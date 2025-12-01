#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include "../Part2/buffer_pool.hpp"

static constexpr uint32_t SST_PAGE_SIZE  = 4096;
static constexpr uint32_t BTREE_HDR_SIZE = 8; // [0]=type, [1..3]=pad, [4..7]=numKeys

// enum for SSTable search mode (B tree or flat SST)
enum class SSTSearchMode {
    Binary,
    BTree
};

// TODO: consider fd management, should we keep it open or close after each operation?
// currently keeping it open for simplicity
template <typename K, typename V>
class SSTable {
    BufferPool* bufferPool_;
    std::string filepath_;
    int fd_;
    size_t maxSize_;
    const size_t pairSize_ = sizeof(K) + sizeof(V); // Assuming fixed-size K and V
    SSTSearchMode mode_;

    // helpers for flat layout
    std::pair<K, V> readPairBinary(size_t index) const {
        uint64_t offset = index * pairSize_;

        // page-aligned path if we have a buffer pool
        if (bufferPool_) {
            uint64_t pageOffset = (offset / SST_PAGE_SIZE) * SST_PAGE_SIZE;
            uint64_t inside = offset % SST_PAGE_SIZE;
            const char* page = bufferPool_->getPage(PageId{filepath_, pageOffset});

            K k; V v;
            std::memcpy(&k, page + inside, sizeof(K));
            std::memcpy(&v, page + inside + sizeof(K), sizeof(V));
            return {k, v};
        }

        // direct pread if no buffer pool
        char buf[sizeof(K) + sizeof(V)];
        ssize_t bytes = ::pread(fd_, buf, sizeof(buf), static_cast<off_t>(offset));
        if (bytes != (ssize_t)sizeof(buf)) {
            throw std::runtime_error(
                "pread failed for entry at index " + std::to_string(index) +
                " in " + filepath_);
        }
        K k; V v;
        std::memcpy(&k, buf, sizeof(K));
        std::memcpy(&v, buf + sizeof(K), sizeof(V));
        return {k, v};
    }

    // helpers for B-tree layout
    // Load a page from disk if we do not have a buffer pool.
    const char* loadPageOS(uint64_t pageOffset) const {
        static char buf[SST_PAGE_SIZE];
        ssize_t bytes = pread(fd_, buf, SST_PAGE_SIZE, static_cast<off_t>(pageOffset));
        if (bytes != (ssize_t)SST_PAGE_SIZE) {
            throw std::runtime_error(
                "Read less than expected in B-tree pread for " + filepath_);
        }
        return buf;
    }

    const char* getPageAt(uint64_t pageOffset) const {
        if (bufferPool_) {
            return bufferPool_->getPage(PageId{filepath_, pageOffset});
        }
        return loadPageOS(pageOffset);
    }

    // B-tree search - root at offset 0
    V* getBTree(const K& key) const {
        if (fd_ == -1) return nullptr;

        uint64_t offset = 0; // root page is at 0

        while (true) {
            const char* page = getPageAt(offset);

            uint8_t nodeType = 0; // 1 = internal, 2 = leaf
            std::memcpy(&nodeType, page, sizeof(uint8_t));

            uint32_t numKeys = 0;
            std::memcpy(&numKeys, page + 4, sizeof(uint32_t));

            if (nodeType == 2) { // leaf node
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
            else if (nodeType == 1) { // internal node
                size_t keyStart = BTREE_HDR_SIZE;
                size_t childStart = BTREE_HDR_SIZE + (size_t)numKeys * sizeof(K);

                // Binary search on delimeter keys
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
                size_t childIndex = lo; // in [0..numKeys]
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

    // build static B-tree and write to disk
    void writeFromPairsBTree(const std::vector<std::pair<K, V>>& entries) {
        if (entries.empty()) {
            // truncate file to zero size, mark empty
            std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
            file.close();
            maxSize_ = 0;
            fd_ = open(filepath_.c_str(), O_RDONLY);
            return;
        }

        struct Node {
            bool isLeaf = false;
            K minKey{};
            std::vector<int> children; // indices of child nodes for internal nodes
            std::vector<K> keys; // delimarator keys (internal nodes)
            std::vector<std::pair<K,V>> leafEntries; // actual data (leaf nodes)
        };

        std::vector<Node> nodes;

        // build leaves
        const size_t leafCap = (SST_PAGE_SIZE - BTREE_HDR_SIZE) / pairSize_;
        if (leafCap == 0) {
            throw std::runtime_error("pairSize too large to fit in 1 page");
        }

        size_t n = entries.size();
        size_t pos = 0;
        std::vector<int> currentLevel;

        // insert leaves
        while (pos < n) {
            size_t cnt = std::min(leafCap, n - pos);
            Node leaf;
            leaf.isLeaf = true;
            leaf.leafEntries.insert(leaf.leafEntries.end(),
                                    entries.begin() + pos,
                                    entries.begin() + pos + cnt);
            leaf.minKey = leaf.leafEntries.front().first; // min key in this leaf

            int idx = (int)nodes.size();
            nodes.push_back(std::move(leaf));
            currentLevel.push_back(idx);

            pos += cnt;
        }

        // build internal levels bottom-up
        const size_t maxKeysInternal = std::max<size_t>(
            1, (SST_PAGE_SIZE - BTREE_HDR_SIZE - sizeof(uint64_t))
                   / (sizeof(K) + sizeof(uint64_t))); // key + child ptr pairs per page (at least 1)

        const size_t maxChildren = maxKeysInternal + 1;

        while (currentLevel.size() > 1) {
            std::vector<int> nextLevel;
            size_t i = 0;
            while (i < currentLevel.size()) {
                size_t groupSize = std::min(maxChildren, currentLevel.size() - i);

                // at least 2 children per internal node when possible
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

        // put root at node index 0 so its pageOffset = 0
        if (rootIndex != 0) {
            std::swap(nodes[0], nodes[rootIndex]);
            int a = 0;
            int b = rootIndex;
            for (auto& n : nodes) {
                for (int& c : n.children) {
                    if (c == a) {
                        c = b;
                    }
                    else if (c == b){
                        c = a;
                    } 
                }
            }
        }

        // write nodes to disk, page i at offset i * PAGE_SIZE
        std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open " + filepath_ + " for B-tree writing");
        }

        maxSize_ = entries.size();

        // write nodes
        for (size_t i = 0; i < nodes.size(); ++i) {
            char page[SST_PAGE_SIZE];
            std::memset(page, 0, SST_PAGE_SIZE);

            Node& node = nodes[i];

            uint8_t nodeType = node.isLeaf ? 2 : 1; // 1=internal, 2=leaf
            std::memcpy(page, &nodeType, sizeof(uint8_t));

            uint32_t numKeys;
            
            if (node.isLeaf) {
                numKeys = (uint32_t)node.leafEntries.size();
            }
            else {
                numKeys = (uint32_t)node.keys.size();
            }
            
            std::memcpy(page + 4, &numKeys, sizeof(uint32_t));
            
            // write leaves to page
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
                // keys
                for (const K& key : node.keys) {
                    std::memcpy(page + off, &key, sizeof(K));
                    off += sizeof(K);
                }
                // children offsets
                for (int childIdx : node.children) {
                    uint64_t childOffset = (uint64_t)childIdx * SST_PAGE_SIZE;
                    std::memcpy(page + off, &childOffset, sizeof(uint64_t));
                    off += sizeof(uint64_t);
                }
            }

            file.write(page, SST_PAGE_SIZE);
        }

        file.close();

        fd_ = open(filepath_.c_str(), O_RDONLY); // need to do direct I/O
        if (fd_ == -1) {
            throw std::runtime_error("Could not reopen " + filepath_ + " after B-tree write");
        }
    }

public:

    // have 2 SSTables constructors: one with buffer pool, one without
    SSTable(const std::string& filepath, size_t maxSize, BufferPool* bp,
            SSTSearchMode mode = SSTSearchMode::Binary)
        : bufferPool_(bp),
          filepath_(filepath),
          fd_(-1),
          maxSize_(maxSize),
          mode_(mode)
    {
        fd_ = ::open(filepath.c_str(), O_RDONLY);
    }

    SSTable(const std::string& filepath, size_t maxSize)
        : bufferPool_(nullptr),
          filepath_(filepath),
          fd_(-1),
          maxSize_(maxSize),
          mode_(SSTSearchMode::Binary)
    {
        fd_ = ::open(filepath.c_str(), O_RDONLY);
    }

    // Write SST from sorted Key-Value pairs to disk
    void writeFromPairs(const std::vector<std::pair<K, V>>& entries) {
        // flat SST
        if (mode_ == SSTSearchMode::Binary) {
            std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
            if (!file.is_open())
                throw std::runtime_error("Could not open " + filepath_);

            for (auto& [key, value] : entries) {
                file.write(reinterpret_cast<const char*>(&key),   sizeof(K));
                file.write(reinterpret_cast<const char*>(&value), sizeof(V));
            }

            maxSize_ = entries.size();

            // pad to 4096-byte page boundary
            size_t bytesUsed  = entries.size() * pairSize_;
            size_t remainder  = bytesUsed % SST_PAGE_SIZE;
            if (remainder != 0) {
                size_t pad = SST_PAGE_SIZE - remainder;
                std::vector<char> zeros(pad, 0);
                file.write(zeros.data(), pad);
            }

            file.close();

            fd_ = ::open(filepath_.c_str(), O_RDONLY);
            if (fd_ == -1)
                throw std::runtime_error("Could not reopen " + filepath_);
        } 
        else { // static B-tree
            writeFromPairsBTree(entries);
        }
    }

    V* get(const K& key) const {
        if (fd_ == -1) { 
            return nullptr;
        }
        
        // binary search over flat SST
        if (mode_ == SSTSearchMode::Binary) {
            size_t left = 0;
            size_t right = maxSize_;

            while (left < right) {
                size_t mid = (left + right) / 2;
                auto [midKey, midVal] = readPairBinary(mid);

                if (midKey == key)
                    return new V(midVal);
                if (midKey < key)
                    left = mid + 1;
                else
                    right = mid;
            }
            return nullptr;
        } 
        else {
            return getBTree(key);
        }
    }

    std::vector<std::pair<K, V>> scan(const K& start, const K& end) {
        std::vector<std::pair<K, V>> result;
        if (fd_ == -1)
            return result;

        // binary search to find starting index
        if (mode_ == SSTSearchMode::Binary) {
            size_t left = 0;
            size_t right = maxSize_;
            while (left < right) {
                size_t mid = (left + right) / 2;
                auto [midKey, _] = readPairBinary(mid);
                if (midKey < start)
                    left = mid + 1;
                else
                    right = mid;
            }

            // Sequential reads until out of range
            for (size_t i = left; i < maxSize_; ++i) {
                auto kv = readPairBinary(i);
                if (kv.first > end)
                    break;
                result.push_back(kv);
            }
            return result;
        } else {
            // TODO: implement B-tree scan
            throw std::runtime_error("scan() for B-tree SST not implemented yet");
        }
    }

    const std::string& path() { return filepath_; }
};
