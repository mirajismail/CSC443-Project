#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
#include "../Part2/buffer_pool.hpp"

// TODO: consider fd management, should we keep it open or close after each operation?
// currently keeping it open for simplicity
template <typename K, typename V>
class SSTable {
    BufferPool* bufferPool_;
    std::string filepath_;
    int fd_;
    size_t maxSize_; // number of entries when memtable flushes
    size_t pairSize_ = sizeof(K) + sizeof(V); // Assuming fixed-size K and V for simplicity
    
    std::pair<K, V> readPair(size_t index) const {
        uint64_t offset = index * pairSize_;

        uint64_t pageOffset = (offset / 4096) * 4096;
        uint64_t inside = offset % 4096;

        if (!bufferPool_) {
            // fall back to pread if no buffer pool
            char buf[sizeof(K) + sizeof(V)];
            ssize_t bytes = ::pread(fd_, buf, sizeof(buf), static_cast<off_t>(offset));
            if (bytes != (ssize_t)sizeof(buf)) {
                throw std::runtime_error("pread failed for entry at index " + std::to_string(index)
                                         + " in " + filepath_);
            }
            K k; V v;
            std::memcpy(&k, buf, sizeof(K));
            std::memcpy(&v, buf + sizeof(K), sizeof(V));
            return {k, v};
        }

        const char* page = bufferPool_->getPage(PageId{filepath_, pageOffset});

        K k; V v;
        std::memcpy(&k, page + inside, sizeof(K));
        std::memcpy(&v, page + inside + sizeof(K), sizeof(V));
        return {k, v};
    }

public:
    SSTable(const std::string& filepath, size_t maxSize, BufferPool* bp)
        : bufferPool_(bp), filepath_(filepath), fd_(-1), maxSize_(maxSize)
    {
        fd_ = open(filepath.c_str(), O_RDONLY);
    }

    SSTable(const std::string& filepath, size_t maxSize)
        : bufferPool_(nullptr), filepath_(filepath), fd_(-1), maxSize_(maxSize)
    {
        fd_ = open(filepath.c_str(), O_RDONLY);
    }



    // Write SST from sorted Key-Value pairs to disk
    void writeFromPairs(const std::vector<std::pair<K, V>>& entries) {
        std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("Could not open " + filepath_);

        for (auto& [key, value] : entries) {
            file.write(reinterpret_cast<const char*>(&key), sizeof(K));
            file.write(reinterpret_cast<const char*>(&value), sizeof(V));
        }

        // IMPORTANT: true #entries (not memtable max)
        maxSize_ = entries.size();

        // pad to 4096-byte page
        size_t bytesUsed = entries.size() * pairSize_;
        size_t remainder = bytesUsed % 4096;
        if (remainder != 0) {
            size_t pad = 4096 - remainder;
            std::vector<char> zeros(pad, 0);
            file.write(zeros.data(), pad);
        }

        file.close();

        fd_ = open(filepath_.c_str(), O_RDONLY);
        if (fd_ == -1)
            throw std::runtime_error("Could not reopen " + filepath_);
    }



    V* get(const K& key) const {
        if (fd_ == -1)
            return nullptr;

        // left and right are indices of the entries
        size_t left = 0;
        size_t right = maxSize_;

        while (left < right) {
            size_t mid = (left + right) / 2;
            auto [midKey, midVal] = readPair(mid);

            if (midKey == key)
                return new V(midVal);
            if (midKey < key)
                left = mid + 1;
            else
                right = mid;
        }
        return nullptr;
    }

    std::vector<std::pair<K, V>> scan(const K& start, const K& end) {
        std::vector<std::pair<K, V>> result;
        if (fd_ == -1)
            return result; // return empty if file doesn't exist

        // Binary search to find starting index
        size_t left = 0;
        size_t right = maxSize_;
        while (left < right) {
            size_t mid = (left + right) / 2;
            auto [midKey, _] = readPair(mid); // TODO: would it be better to have readKey for this?
            if (midKey < start)
                left = mid + 1;
            else
                right = mid;
        }

        // Sequential reads until out of range
        for (size_t i = left; i < maxSize_; ++i) {
            std::pair<K, V> pair = readPair(i);
            if (pair.first > end)
                break;
            result.push_back(pair);
        }

        return result;
    }

    const std::string& path() { return filepath_; }
};

