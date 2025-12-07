#pragma once
#include "sst_base.hpp"

// Static B-tree SSTable
// Page layout: [nodeType:1][pad:3][numKeys:4][keys...][children/values...]
template <typename K, typename V>
class BTreeSST : public SSTBase<K, V> {
    using SSTBase<K, V>::bufferPool_;
    using SSTBase<K, V>::filepath_;
    using SSTBase<K, V>::fd_;
    using SSTBase<K, V>::maxSize_;
    using SSTBase<K, V>::pairSize_;
    using SSTBase<K, V>::getPageAt;
    using SSTBase<K, V>::openFlags_;

    // traverse from root to leaf, binary search within each node
    V* searchBTree(const K& key) const {
        if (fd_ == -1) return nullptr;

        uint64_t offset = 0;

        while (true) {
            const char* page = getPageAt(offset);

            uint8_t nodeType = 0;
            std::memcpy(&nodeType, page, sizeof(uint8_t));

            uint32_t numKeys = 0;
            std::memcpy(&numKeys, page + 4, sizeof(uint32_t));

            if (nodeType == 2) { // leaf
                size_t left = 0, right = numKeys;

                while (left < right) {
                    size_t mid = (left + right) / 2;
                    size_t entryOffset = BTREE_HDR_SIZE + mid * pairSize_;

                    K midKey;
                    std::memcpy(&midKey, page + entryOffset, sizeof(K));
                    if (midKey == key) {
                        V value;
                        std::memcpy(&value, page + entryOffset + sizeof(K), sizeof(V));
                        return new V(value);
                    }
                    if (midKey < key) {
                        left = mid + 1;
                    }
                    else {
                        right = mid;
                    }
                }
                return nullptr;
            }
            else if (nodeType == 1) { // internal
                size_t keyStart = BTREE_HDR_SIZE;
                size_t childStart = BTREE_HDR_SIZE + (size_t)numKeys * sizeof(K);

                size_t lo = 0, hi = numKeys;
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    K delim;
                    std::memcpy(&delim, page + keyStart + mid * sizeof(K), sizeof(K));
                    if (key < delim) {
                        hi = mid;
                    }
                    else {
                        lo = mid + 1;
                    }
                }
                size_t childIndex = lo;
                uint64_t childOffset = 0;
                std::memcpy(&childOffset,
                            page + childStart + childIndex * sizeof(uint64_t),
                            sizeof(uint64_t));
                offset = childOffset;
                continue;
            }
            else {
                throw std::runtime_error("Unknown B-tree node type in " + filepath_);
            }
        }
    }

    // build tree bottom-up (create leaves, then internal nodes until root)
    void writeBTree(const std::vector<std::pair<K, V>>& entries) {
        if (entries.empty()) {
            std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
            file.close();
            maxSize_ = 0;
            if (fd_ != -1) close(fd_);
            fd_ = open(filepath_.c_str(), O_RDONLY);
            return;
        }

        struct Node {
            bool isLeaf = false;
            K minKey{};
            std::vector<int> children;
            std::vector<K> keys;
            std::vector<std::pair<K,V>> leafEntries;
        };

        std::vector<Node> nodes;

        const size_t leafCap = (SST_PAGE_SIZE - BTREE_HDR_SIZE) / pairSize_;
        if (leafCap == 0) {
            throw std::runtime_error("pairSize too large to fit in 1 page");
        }

        size_t n = entries.size();
        size_t pos = 0;
        std::vector<int> currentLevel;

        while (pos < n) {
            size_t cnt = std::min(leafCap, n - pos);
            Node leaf;
            leaf.isLeaf = true;
            leaf.leafEntries.insert(leaf.leafEntries.end(),
                                    entries.begin() + pos,
                                    entries.begin() + pos + cnt);
            leaf.minKey = leaf.leafEntries.front().first;

            int idx = (int)nodes.size();
            nodes.push_back(std::move(leaf));
            currentLevel.push_back(idx);

            pos += cnt;
        }

        const size_t maxKeysInternal = std::max<size_t>(
            1, (SST_PAGE_SIZE - BTREE_HDR_SIZE - sizeof(uint64_t))
                   / (sizeof(K) + sizeof(uint64_t)));

        const size_t maxChildren = maxKeysInternal + 1;

        while (currentLevel.size() > 1) {
            std::vector<int> nextLevel;
            size_t i = 0;
            while (i < currentLevel.size()) {
                size_t groupSize = std::min(maxChildren, currentLevel.size() - i);

                if (groupSize < 2 && currentLevel.size() > 1) {
                    groupSize = std::min<size_t>(2, currentLevel.size() - i);
                }

                Node parent;
                parent.isLeaf = false;

                for (size_t j = 0; j < groupSize; ++j) {
                    int childIdx = currentLevel[i + j];
                    parent.children.push_back(childIdx);
                    if (j > 0) {
                        parent.keys.push_back(nodes[childIdx].minKey);
                    }
                }
                parent.minKey = nodes[parent.children.front()].minKey;

                int idx = (int)nodes.size();
                nodes.push_back(std::move(parent));
                nextLevel.push_back(idx);

                i += groupSize;
            }
            currentLevel.swap(nextLevel);
        }

        int rootIndex = currentLevel[0];

        // swap root to index 0 so it lives at page offset 0
        if (rootIndex != 0) {
            std::swap(nodes[0], nodes[rootIndex]);
            int a = 0;
            int b = rootIndex;
            for (auto& nd : nodes) {
                for (int& c : nd.children) {
                    if (c == a) c = b;
                    else if (c == b) c = a;
                }
            }
        }

        std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open " + filepath_ + " for B-tree writing");
        }

        maxSize_ = entries.size();

        for (size_t i = 0; i < nodes.size(); ++i) {
            char page[SST_PAGE_SIZE];
            std::memset(page, 0, SST_PAGE_SIZE);

            Node& node = nodes[i];

            uint8_t nodeType = node.isLeaf ? 2 : 1;
            std::memcpy(page, &nodeType, sizeof(uint8_t));

            uint32_t numKeys;
            if (node.isLeaf) {
                numKeys = (uint32_t)node.leafEntries.size();
            }
            else {
                numKeys = (uint32_t)node.keys.size();
            }

            std::memcpy(page + 4, &numKeys, sizeof(uint32_t));

            if (node.isLeaf) {
                size_t off = BTREE_HDR_SIZE;
                for (auto& [key, value] : node.leafEntries) {
                    std::memcpy(page + off, &key,  sizeof(K));
                    off += sizeof(K);
                    std::memcpy(page + off, &value, sizeof(V));
                    off += sizeof(V);
                }
            }
            else {
                size_t off = BTREE_HDR_SIZE;
                for (const K& key : node.keys) {
                    std::memcpy(page + off, &key, sizeof(K));
                    off += sizeof(K);
                }
                for (int childIdx : node.children) {
                    uint64_t childOffset = (uint64_t)childIdx * SST_PAGE_SIZE;
                    std::memcpy(page + off, &childOffset, sizeof(uint64_t));
                    off += sizeof(uint64_t);
                }
            }

            file.write(page, SST_PAGE_SIZE);
        }

        file.close();

        if (fd_ != -1) close(fd_);
        fd_ = open(filepath_.c_str(), openFlags_);
        if (fd_ == -1) {
            throw std::runtime_error("Could not reopen " + filepath_ + " after B-tree write");
        }
    }

public:
    BTreeSST(const std::string& filepath, size_t maxSize, BufferPool* bp = nullptr, bool useDirect = false)
        : SSTBase<K, V>(filepath, maxSize, bp, useDirect) {}

    void writeFromPairs(const std::vector<std::pair<K, V>>& entries) override {
        // build bloom filter for fast negative lookups
        this->buildBloomFilter(entries);

        writeBTree(entries);

        // write bloom filter to file
        std::ofstream bloom_file(filepath_ + ".bloom", std::ios::trunc | std::ios::binary);
        if (bloom_file.is_open()) {
            this->writeBloomToFile(bloom_file);
            bloom_file.close();
        }
    }

    V* get(const K& key) const override {
        // bloom filter check
        if (!this->contains(key)) return nullptr;

        return searchBTree(key);
    }

    std::vector<std::pair<K, V>> scan(const K& start, const K& end) override {
        std::vector<std::pair<K, V>> result;
        if (fd_ == -1) return result;

        // find leaf containing start key
        uint64_t offset = 0;
        while (true) {
            const char* page = getPageAt(offset);

            uint8_t nodeType = 0;
            std::memcpy(&nodeType, page, sizeof(uint8_t));

            uint32_t numKeys = 0;
            std::memcpy(&numKeys, page + 4, sizeof(uint32_t));

            if (nodeType == 2) { // leaf - found it
                break;
            }
            else if (nodeType == 1) { // internal
                size_t keyStart = BTREE_HDR_SIZE;
                size_t childStart = BTREE_HDR_SIZE + (size_t)numKeys * sizeof(K);

                size_t lo = 0, hi = numKeys;
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    K delim;
                    std::memcpy(&delim, page + keyStart + mid * sizeof(K), sizeof(K));
                    if (start < delim) {
                        hi = mid;
                    }
                    else {
                        lo = mid + 1;
                    }
                }
                uint64_t childOffset = 0;
                std::memcpy(&childOffset,
                            page + childStart + lo * sizeof(uint64_t),
                            sizeof(uint64_t));
                offset = childOffset;
            }
            else {
                throw std::runtime_error("Unknown B-tree node type in " + filepath_);
            }
        }

        // sequential scan through leaf pages
        while (true) {
            const char* page = getPageAt(offset);

            uint8_t nodeType = 0;
            std::memcpy(&nodeType, page, sizeof(uint8_t));
            if (nodeType != 2) break; // not a leaf anymore

            uint32_t numKeys = 0;
            std::memcpy(&numKeys, page + 4, sizeof(uint32_t));

            for (uint32_t i = 0; i < numKeys; ++i) {
                size_t entryOffset = BTREE_HDR_SIZE + i * pairSize_;

                K key;
                std::memcpy(&key, page + entryOffset, sizeof(K));

                if (key > end) {
                    return result;
                }
                if (key >= start) {
                    V value;
                    std::memcpy(&value, page + entryOffset + sizeof(K), sizeof(V));
                    result.push_back({key, value});
                }
            }

            offset += SST_PAGE_SIZE;
        }

        return result;
    }
};
