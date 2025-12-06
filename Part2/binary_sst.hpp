#pragma once
#include "sst_base.hpp"

// Flat sorted array SSTable with binary search for lookups
template <typename K, typename V>
class BinarySST : public SSTBase<K, V> {
    using SSTBase<K, V>::bufferPool_;
    using SSTBase<K, V>::filepath_;
    using SSTBase<K, V>::fd_;
    using SSTBase<K, V>::maxSize_;
    using SSTBase<K, V>::pairSize_;
    using SSTBase<K, V>::openFlags_;

    std::pair<K, V> readPairBinary(size_t index) const {
        uint64_t offset = index * pairSize_;

        if (bufferPool_) {
            uint64_t pageOffset = (offset / SST_PAGE_SIZE) * SST_PAGE_SIZE;
            uint64_t inside = offset % SST_PAGE_SIZE;
            const char* page = bufferPool_->getPage(PageId{filepath_, pageOffset});

            K k; V v;
            std::memcpy(&k, page + inside, sizeof(K));
            std::memcpy(&v, page + inside + sizeof(K), sizeof(V));
            return {k, v};
        }

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

public:
    BinarySST(const std::string& filepath, size_t maxSize, BufferPool* bp = nullptr, bool useDirect = false)
        : SSTBase<K, V>(filepath, maxSize, bp, useDirect) {}

    // write sorted pairs sequentially, pad to page boundary
    void writeFromPairs(const std::vector<std::pair<K, V>>& entries) override {
        std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("Could not open " + filepath_);

        for (auto& [key, value] : entries) {
            file.write(reinterpret_cast<const char*>(&key),   sizeof(K));
            file.write(reinterpret_cast<const char*>(&value), sizeof(V));
        }

        maxSize_ = entries.size();

        size_t bytesUsed  = entries.size() * pairSize_;
        size_t remainder  = bytesUsed % SST_PAGE_SIZE;
        if (remainder != 0) {
            size_t pad = SST_PAGE_SIZE - remainder;
            std::vector<char> zeros(pad, 0);
            file.write(zeros.data(), pad);
        }

        file.close();

        if (fd_ != -1) ::close(fd_);
        fd_ = ::open(filepath_.c_str(), openFlags_);
        if (fd_ == -1)
            throw std::runtime_error("Could not reopen " + filepath_);
    }

    // binary search over sorted entries
    V* get(const K& key) const override {
        if (fd_ == -1) return nullptr;

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

    // find start position with binary search, then scan sequentially
    std::vector<std::pair<K, V>> scan(const K& start, const K& end) override {
        std::vector<std::pair<K, V>> result;
        if (fd_ == -1) return result;

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

        for (size_t i = left; i < maxSize_; ++i) {
            auto kv = readPairBinary(i);
            if (kv.first > end)
                break;
            result.push_back(kv);
        }
        return result;
    }
};
