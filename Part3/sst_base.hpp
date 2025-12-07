#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstdint>
#include <memory>
#include "buffer_pool.hpp"
#include "bloom_filter.hpp"

static constexpr uint32_t SST_PAGE_SIZE  = 4096;
static constexpr uint32_t BTREE_HDR_SIZE = 8; // [0]=type, [1..3]=pad, [4..7]=numKeys

enum class SSTSearchMode {
    Binary,
    BTree
};


// Base class for SSTables
template <typename K, typename V>
class SSTBase {

// protected so that derived classes can access
protected:
    BufferPool* bufferPool_;
    std::string filepath_;
    int fd_;
    size_t maxSize_;
    const size_t pairSize_ = sizeof(K) + sizeof(V);
    int openFlags_;
    std::unique_ptr<BloomFilter> bloom_;

    const char* getPageAt(uint64_t pageOffset) const {
        if (bufferPool_) {
            return bufferPool_->getPage(PageId{filepath_, pageOffset});
        }
        return loadPageOS(pageOffset);
    }

    const char* loadPageOS(uint64_t pageOffset) const {
        static char buf[SST_PAGE_SIZE];
        ssize_t bytes = pread(fd_, buf, SST_PAGE_SIZE, static_cast<off_t>(pageOffset));
        if (bytes != (ssize_t)SST_PAGE_SIZE) {
            throw std::runtime_error(
                "Read less than expected in pread for " + filepath_);
        }
        return buf;
    }

    // create bloom filter from entries during write
    // bits_per_entry=0 uses default fp_rate calculation
    void buildBloomFilter(const std::vector<std::pair<K, V>>& entries, size_t bits_per_entry = 0) {
        if (bits_per_entry > 0) {
            bloom_ = std::make_unique<BloomFilter>(entries.size(), bits_per_entry);
        } else {
            bloom_ = std::make_unique<BloomFilter>(entries.size());
        }
        for (const auto& [k, v] : entries) {
            bloom_->add(k);
        }
    }

    // write bloom filter to file
    void writeBloomToFile(std::ofstream& out) {
        if (!bloom_) return;
        
        // header: 4 bytes for bloom size
        uint32_t bloom_bytes = static_cast<uint32_t>(bloom_->bloomFileSize());
        out.write(reinterpret_cast<char*>(&bloom_bytes), sizeof(bloom_bytes));
        
        // write bloom
        std::vector<uint8_t> buf(bloom_bytes);
        bloom_->bloomWrite(buf.data());
        out.write(reinterpret_cast<char*>(buf.data()), bloom_bytes);
    }

    // load bloom filter from file
    void loadBloomFromFile() {
        std::string bloom_path = filepath_ + ".bloom";
        std::ifstream bloom_file(bloom_path, std::ios::binary);
        if (!bloom_file.is_open()) return;
        
        uint32_t bloom_bytes;
        bloom_file.read(reinterpret_cast<char*>(&bloom_bytes), sizeof(bloom_bytes));
        
        if (bloom_bytes == 0) {
            return;  
        }
        
        std::vector<uint8_t> buf(bloom_bytes);
        bloom_file.read(reinterpret_cast<char*>(buf.data()), bloom_bytes);
        
        bloom_ = std::make_unique<BloomFilter>(BloomFilter::bloomLoad(buf.data()));
    }

public:
    SSTBase(const std::string& filepath, size_t maxSize, BufferPool* bp, bool useDirect = false)
        : bufferPool_(bp),
          filepath_(filepath),
          fd_(-1),
          maxSize_(maxSize),
          openFlags_(useDirect ? (O_RDONLY | O_DIRECT) : O_RDONLY)
    {
        fd_ = open(filepath.c_str(), openFlags_);
        if (fd_ != -1) {
            loadBloomFromFile();
        }
    }

    virtual ~SSTBase() {
        if (fd_ != -1) close(fd_);
    }

    // check bloom filter before searching
    bool contains(const K& key) const {
        if (!bloom_) return true;  // no filter, must search
        return bloom_->contains(key);
    }

    virtual void writeFromPairs(const std::vector<std::pair<K, V>>& entries) = 0;
    virtual V* get(const K& key) const = 0;
    virtual std::vector<std::pair<K, V>> scan(const K& start, const K& end) = 0;

    const std::string& path() const { return filepath_; }
    size_t size() const { return maxSize_; }
};
