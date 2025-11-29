#pragma once
#include <iostream>
#include <vector>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <filesystem>
#include <unistd.h>

struct Record {
    int64_t key;
    int64_t value;
};

static_assert(sizeof(Record) == sizeof(int64_t)*2, "Rec must be fixed-size");

// loop until all bytes are written
inline void write_full(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0) throw std::runtime_error(std::string("write: ")+std::strerror(errno));
        p += w; n -= (size_t)w;
    }
}

// read exactly N bytes starting at off, without moving a shared file pointer
inline void pread_full(int fd, void* buf, size_t n, off_t off) {
    char* p = static_cast<char*>(buf);
    while (n) {
        ssize_t r = ::pread(fd, p, n, off);
        if (r <= 0) throw std::runtime_error(std::string("pread: ")+std::strerror(errno));
        p += r; n -= (size_t)r; off += r;
    }
}