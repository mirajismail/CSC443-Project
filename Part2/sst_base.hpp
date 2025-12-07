#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstdint>
#include "buffer_pool.hpp"

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

public:
    SSTBase(const std::string& filepath, size_t maxSize, BufferPool* bp, bool useDirect = false)
        : bufferPool_(bp),
          filepath_(filepath),
          fd_(-1),
          maxSize_(maxSize),
          openFlags_(useDirect ? (O_RDONLY | O_DIRECT) : O_RDONLY)
    {
        fd_ = open(filepath.c_str(), openFlags_);
    }

    // virtual functions for derived classes

    virtual ~SSTBase() {
        if (fd_ != -1) close(fd_);
    }

    virtual void writeFromPairs(const std::vector<std::pair<K, V>>& entries) = 0;
    virtual V* get(const K& key) const = 0;
    virtual std::vector<std::pair<K, V>> scan(const K& start, const K& end) = 0;

    const std::string& path() const { return filepath_; }
    size_t size() const { return maxSize_; }
};
