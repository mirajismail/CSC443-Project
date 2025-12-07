#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct PageId {
    std::string file;  // SST filename
    uint64_t offset;   // byte offset in the file (4kB aligned)

    bool operator==(const PageId& other) const {
        return offset == other.offset && file == other.file;
    }
};

class BufferPool {
public:
    static constexpr size_t PAGE_SIZE = 4096;

    explicit BufferPool(size_t capacityFrames);
    const char* getPage(const PageId& pid);
    
    // invalidate all cached pages for a file
    void invalidateFile(const std::string& file);

    // stats for testing
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;

    // For test code to reset values
    void resetStats() {
        hits = misses = evictions = 0;
    }


    size_t size() const { return usedFrames; }
    size_t capacity() const { return capacityFrames; }

private:
    struct alignas(4096) Frame { // destination buffer must be aligned to 4 KB
        PageId id;
        bool valid = false;
        bool referenced = false;
        char data[PAGE_SIZE];

        // hash table chaining
        Frame* hashNext = nullptr;

        // clock circular list
        Frame* clockPrev = nullptr;
        Frame* clockNext = nullptr;
    };

    std::vector<Frame> frames;
    size_t capacityFrames;
    size_t usedFrames;  // number of frames that have ever been allocated

    std::vector<Frame*> table; // hash buckets
    Frame* clockCur; // current position for clock eviction


    // helper methods

    Frame* lookup(const PageId& pid);
    Frame* allocateNewFrame(); // for usedFrames < capacityFrames
    Frame* evictOne(); // clock eviction, returns reusable frame*

    Frame* loadPageIntoFrame(Frame* f, const PageId& pid);

    size_t bucketIndex(const PageId& pid) const;
    std::size_t hash(const PageId& pid) const;

    void insertIntoHash(Frame* f);
    void removeFromHash(Frame* f);

    void insertIntoClockList(Frame* f);
};
