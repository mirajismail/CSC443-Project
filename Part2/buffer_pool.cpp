#include "buffer_pool.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>

BufferPool::BufferPool(size_t capacityFrames_)
    : capacityFrames(capacityFrames_),
      usedFrames(0),
      clockHand(nullptr)
{
    if (capacityFrames == 0) {
        throw std::invalid_argument("BufferPool capacityFrames must be > 0");
    }

    frames.resize(capacityFrames);

    // choose hash table size ~2x capacity
    size_t numBuckets = capacityFrames * 2;
    if (numBuckets == 0) numBuckets = 1;
    table.assign(numBuckets, nullptr);
}

// simple hash combiner
std::size_t BufferPool::hash(const PageId& pid) const {
    std::hash<std::string> h1;
    std::hash<uint64_t> h2;
    std::size_t seed = h1(pid.file);
    seed ^= h2(pid.offset) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
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
            if (prev) prev->hashNext = cur->hashNext;
            else      table[idx] = cur->hashNext;
            f->hashNext = nullptr;
            return;
        }
        prev = cur;
        cur = cur->hashNext;
    }
}

// insert frame into circular CLOCK list
void BufferPool::insertIntoClockList(Frame* f) {
    if (!clockHand) {
        clockHand = f;
        f->clockNext = f;
        f->clockPrev = f;
        return;
    }
    // insert after clockHand
    Frame* next = clockHand->clockNext;
    clockHand->clockNext = f;
    f->clockPrev = clockHand;
    f->clockNext = next;
    next->clockPrev = f;
}

// allocate next unused frame (no eviction)
BufferPool::Frame* BufferPool::allocateNewFrame() {
    if (usedFrames >= capacityFrames) return nullptr;
    Frame* f = &frames[usedFrames++];
    f->valid = false;
    f->referenced = false;
    f->dirty = false;
    f->hashNext = nullptr;
    f->clockNext = nullptr;
    f->clockPrev = nullptr;
    insertIntoClockList(f);
    return f;
}

// CLOCK eviction: find a frame with referenced == false
BufferPool::Frame* BufferPool::evictOne() {
    if (!clockHand) {
        throw std::runtime_error("BufferPool::evictOne called with empty clock list");
    }

    while (true) {
        Frame* f = clockHand;
        if (!f->valid) {
            // shouldn't really happen, but skip just in case
            clockHand = clockHand->clockNext;
            continue;
        }

        if (f->referenced) {
            f->referenced = false;
            clockHand = clockHand->clockNext;
        } else {
            // victim found
            if (f->dirty) {
                // For step 2, SSTs are read-only so this should not happen.
                // If you later add dirty pages, flush to disk here.
                // (writeback logic to be implemented in Step 3)
                throw std::runtime_error("Dirty pages not supported in BufferPool yet");
            }
            evictions++;
            
            // remove from hash table
            removeFromHash(f);

            // we keep it in clock list, but mark invalid.
            f->valid = false;
            f->referenced = false;

            // move clockHand to next frame
            clockHand = clockHand->clockNext;

            return f;
        }
    }
}

// load page from disk into frame->data, set id and mark valid
BufferPool::Frame* BufferPool::loadPageIntoFrame(Frame* f, const PageId& pid) {
    // open file (you can later cache fds if you care about performance)
    int fd = ::open(pid.file.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Failed to open file " + pid.file + ": " + std::strerror(errno));
    }

    ssize_t nread = ::pread(fd, f->data, PAGE_SIZE, static_cast<off_t>(pid.offset));
    ::close(fd);

    if (nread < 0) {
        throw std::runtime_error("pread failed on file " + pid.file + ": " + std::strerror(errno));
    }
    if (static_cast<size_t>(nread) != PAGE_SIZE) {
        // depending on SST layout, you might want to relax this
        throw std::runtime_error("Short read in BufferPool::loadPageIntoFrame");
    }

    f->id = pid;
    f->valid = true;
    f->referenced = true;
    f->dirty = false;
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

    // 2) miss: need a frame
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
