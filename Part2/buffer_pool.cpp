#include "buffer_pool.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cstring>
#include "xxhash.h"

BufferPool::BufferPool(size_t capacityFrames_)
    : capacityFrames(capacityFrames_),
      usedFrames(0),
      clockCur(nullptr)
{
    if (capacityFrames == 0) {
        throw std::runtime_error("BufferPool capacityFrames must be > 0");
    }

    frames.resize(capacityFrames);
    // choose hash table size 2x capacity
    size_t numBuckets = capacityFrames * 2;
    if (numBuckets == 0) {
        numBuckets = 1;
    }
    table.assign(numBuckets, nullptr); // initialize buckets to nullptr
}

// simple hash combiner using xxhash
std::size_t BufferPool::hash(const PageId& pid) const {
    // buffer format: [file bytes][offset bytes]
    std::string buf;
    buf.append(pid.file);
    buf.append(reinterpret_cast<const char*>(&pid.offset), sizeof(uint64_t));

    return XXH3_64bits(buf.data(), buf.size());
}

size_t BufferPool::bucketIndex(const PageId& pid) const {
    return hash(pid) % table.size();
}

BufferPool::Frame* BufferPool::lookup(const PageId& pid) {
    size_t idx = bucketIndex(pid);
    Frame* f = table[idx];
    while (f) {
        if (f->valid && f->id == pid) {
            hits++;
            return f;
        }
        f = f->hashNext;
    }
    return nullptr;
}

void BufferPool::insertIntoHash(Frame* f) {
    size_t idx = bucketIndex(f->id);
    f->hashNext = table[idx];
    table[idx] = f;
}

void BufferPool::removeFromHash(Frame* f) {
    if (!f->valid) return;
    size_t idx = bucketIndex(f->id);
    Frame* cur = table[idx];
    Frame* prev = nullptr;
    while (cur) {
        if (cur == f) {
            if (prev) {
                prev->hashNext = cur->hashNext;
            }
            else {      
                table[idx] = cur->hashNext;
            }
            f->hashNext = nullptr;
            return;
        }
        prev = cur;
        cur = cur->hashNext;
    }
}

// insert frame into circular clock list
void BufferPool::insertIntoClockList(Frame* f) {
    if (!clockCur) {
        clockCur = f;
        f->clockNext = f;
        f->clockPrev = f;
        return;
    }
    // insert after clockCur
    Frame* next = clockCur->clockNext;
    clockCur->clockNext = f;
    f->clockPrev = clockCur;
    f->clockNext = next;
    next->clockPrev = f;
}

// allocate next unused frame if there's no eviction
BufferPool::Frame* BufferPool::allocateNewFrame() {
    if (usedFrames >= capacityFrames) {
        return nullptr;
    }
    Frame* f = &frames[usedFrames++];
    f->valid = false;
    f->referenced = false;
    f->hashNext = nullptr;
    f->clockNext = nullptr;
    f->clockPrev = nullptr;
    insertIntoClockList(f);
    return f;
}

// clock eviction - find a frame with referenced == false
BufferPool::Frame* BufferPool::evictOne() {
    if (!clockCur) {
        throw std::runtime_error("BufferPool::evictOne called with empty clock list");
    }

    while (true) {
        Frame* f = clockCur;
        if (!f->valid) {
            clockCur = clockCur->clockNext;
            continue;
        }

        if (f->referenced) {
            f->referenced = false;
            clockCur = clockCur->clockNext;
        } 
        else {
            // victim found
            evictions++;            
            removeFromHash(f);

            // mark invalid.
            f->valid = false;
            f->referenced = false;

            clockCur = clockCur->clockNext;

            return f;
        }
    }
}

// load page from disk into frame->data, set id and mark valid
BufferPool::Frame* BufferPool::loadPageIntoFrame(Frame* f, const PageId& pid) {
    // open file
    // use O_DIRECT on linux (Daniel can do that)
    int fd = open(pid.file.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Failed to open file " + pid.file);
    }

    ssize_t nread = pread(fd, f->data, PAGE_SIZE, static_cast<off_t>(pid.offset));
    close(fd);

    if (nread < 0) {
        throw std::runtime_error("pread failed on file " + pid.file);
    }
    if (static_cast<size_t>(nread) != PAGE_SIZE) {
        throw std::runtime_error("Read less than expected in BufferPool::loadPageIntoFrame");
    }

    f->id = pid;
    f->valid = true;
    f->referenced = true;
    f->hashNext = nullptr;

    insertIntoHash(f);
    return f;
}

const char* BufferPool::getPage(const PageId& pid) {
    // 1) lookup
    if (Frame* hit = lookup(pid)) {
        hit->referenced = true;
        return hit->data;
    }

    // 2) miss - need a frame
    misses++;
    Frame* f = nullptr;
    if (usedFrames < capacityFrames) {
        f = allocateNewFrame();
    } else {
        f = evictOne();
    }

    // 3) load from disk
    f = loadPageIntoFrame(f, pid);
    return f->data;
}
