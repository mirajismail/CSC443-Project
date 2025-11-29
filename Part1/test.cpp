// #include <iostream>
// #include <string>
// #include "kvstore.cpp" // Update this include if your header is named differently
// #include "../Part2/buffer_pool.hpp"

// int main() {
//     // Instantiate kvstore<int, int>
//     size_t size = 5;
//     size_t bufferPoolFrames = 128;
//     KVStore<int, int> store {size, bufferPoolFrames};

//     // Test open
//     store.open("teststore");
//     std::cout << "opened" << std::endl;

//     for (int i = 0; i < 9; i++) {
//         store.put(i, i*100);
//     }

//     int* val;
//     for (int i = 0; i < 10; i++) {
//         val = store.get(i);
//         if (val) {
//             std::cout << "Key " << i << " found, value: " << *val << std::endl;
//         } else {
//             std::cout << "Key " << i << " not found." << std::endl;
//         }
//     }

//     // Does not handle duplicate values
//     store.put(0, 100);
//     val = store.get(0);
//     if (val) {
//         std::cout << "Key " << 0 << " found, value: " << *val << std::endl;
//     } else {
//         std::cout << "Key " << 0 << " not found." << std::endl;
//     }

//     val = store.get(5);
//     if (val) {
//         std::cout << "Key " << 5 << " found, value: " << *val << std::endl;
//     } else {
//         std::cout << "Key " << 5 << " not found." << std::endl;
//     }

//     return 0;
// }

// #include <iostream>
// #include <fstream>
// #include <string>
// #include <cassert>
// #include "../Part2/buffer_pool.hpp"

// // Helper: create a binary file with predictable content
// // File contains N pages (4096 bytes each).
// // Page i contains every byte == i (mod 256).
// void createTestFile(const std::string& path, int numPages) {
//     std::ofstream out(path, std::ios::binary | std::ios::trunc);
//     std::vector<char> page(4096);

//     for (int i = 0; i < numPages; i++) {
//         std::fill(page.begin(), page.end(), static_cast<char>(i));
//         out.write(page.data(), page.size());
//     }
//     out.close();
// }

// int main() {
//     std::cout << "=== BUFFER POOL TEST ===\n";

//     const std::string fname = "bp_testfile.bin";
//     const int NUM_PAGES = 20;          // Make many pages
//     const int POOL_FRAMES = 4;         // Tiny pool → forces eviction

//     createTestFile(fname, NUM_PAGES);

//     BufferPool bp(POOL_FRAMES);
//     bp.resetStats();

//     std::cout << "Created file with " << NUM_PAGES 
//               << " pages. BufferPool frames = " << POOL_FRAMES << "\n";

//     auto pageOffset = [&](int pageNum) {
//         return uint64_t(pageNum) * 4096ULL;
//     };

//     std::cout << "\n--- TEST 1: cold misses ---\n";
//     for (int i = 0; i < POOL_FRAMES; i++) {
//         const char* p = bp.getPage({fname, pageOffset(i)});
//         assert(p[0] == (char)i);
//     }

//     std::cout << "After cold loads:\n";
//     std::cout << "  hits = " << bp.hits << "\n";
//     std::cout << "  misses = " << bp.misses << " (expected >= 4)\n";
//     std::cout << "  evictions = " << bp.evictions << "\n";

//     std::cout << "\n--- TEST 2: hot hits (no new pages) ---\n";
//     for (int i = 0; i < POOL_FRAMES; i++) {
//         const char* p = bp.getPage({fname, pageOffset(i)});
//         assert(p[0] == (char)i);
//     }

//     std::cout << "After hot hits:\n";
//     std::cout << "  hits = " << bp.hits << " (expected >= 4)\n";
//     std::cout << "  misses = " << bp.misses << "\n";
//     std::cout << "  evictions = " << bp.evictions << "\n";

//     std::cout << "\n--- TEST 3: force evictions by loading many pages ---\n";
//     for (int i = 0; i < NUM_PAGES; i++) {
//         const char* p = bp.getPage({fname, pageOffset(i)});
//         assert(p[0] == (char)i);
//     }

//     std::cout << "After eviction stress:\n";
//     std::cout << "  hits = " << bp.hits << "\n";
//     std::cout << "  misses = " << bp.misses 
//               << " (should be close to NUM_PAGES = " << NUM_PAGES << ")\n";
//     std::cout << "  evictions = " << bp.evictions 
//               << " (should be > 0 when POOL_FRAMES < NUM_PAGES)\n";

//     std::cout << "\n--- TEST 4: re-access pages to generate mostly hits ---\n";
//     for (int i = NUM_PAGES - 1; i >= NUM_PAGES - POOL_FRAMES; i--) {
//         const char* p = bp.getPage({fname, pageOffset(i)});
//         assert(p[0] == (char)i);
//     }

//     std::cout << "After mixed access:\n";
//     std::cout << "  hits = " << bp.hits << "\n";
//     std::cout << "  misses = " << bp.misses << "\n";
//     std::cout << "  evictions = " << bp.evictions << "\n";

//     std::cout << "\n=== TEST COMPLETE ===\n";
//     return 0;
// }


#include <iostream>
#include <string>
#include "kvstore.cpp"

// Simple helper to check a KVStore get
template <typename K, typename V>
bool check_get(KVStore<K,V>& store, const K& key, const V& expected, const std::string& label) {
    V* v = store.get(key);
    if (!v) {
        std::cout << "[FAIL] " << label << ": key " << key << " missing\n";
        return false;
    }
    if (*v != expected) {
        std::cout << "[FAIL] " << label << ": key " << key 
                  << " has value " << *v << " (expected " << expected << ")\n";
        return false;
    }
    return true;
}

int main() {
    std::cout << "=== KVSTORE + BUFFER POOL TEST ===\n";

    const size_t memtableSize     = 4;   // small → many SST flushes
    const size_t bufferPoolFrames = 4;   // small → force eviction
    const int    NUM_KEYS         = 40;  // enough to create multiple SSTs

    KVStore<int,int> store(memtableSize, bufferPoolFrames);

    // Use a fresh-ish db name to avoid old test files
    store.open("kvbp_store");
    std::cout << "Opened kvbp_store\n";

    // -----------------------
    // Phase 1: PUT workload
    // -----------------------
    std::cout << "\n--- PHASE 1: inserting " << NUM_KEYS << " keys ---\n";
    for (int i = 0; i < NUM_KEYS; ++i) {
        store.put(i, i * 10);
    }
    std::cout << "Insertions complete.\n";

    // Get reference to buffer pool and reset stats
    BufferPool& bp = store.getBufferPool();
    bp.resetStats();

    // -----------------------
    // Phase 2: first full GET round (mostly cold misses)
    // -----------------------
    std::cout << "\n--- PHASE 2: first full GET round (0.." << (NUM_KEYS-1) << ") ---\n";
    int correctCount = 0;
    for (int i = 0; i < NUM_KEYS; ++i) {
        if (check_get(store, i, i * 10, "Phase 2")) {
            correctCount++;
        }
    }
    std::cout << "Correct gets in Phase 2: " << correctCount << " / " << NUM_KEYS << "\n";
    std::cout << "BufferPool stats after Phase 2:\n";
    std::cout << "  hits      = " << bp.hits << "\n";
    std::cout << "  misses    = " << bp.misses << "  (expect > 0)\n";
    std::cout << "  evictions = " << bp.evictions << "  (expect > 0 if many SST pages)\n";

    // -----------------------
    // Phase 3: second full GET round (should be many hits)
    // -----------------------
    std::cout << "\n--- PHASE 3: second full GET round (0.." << (NUM_KEYS-1) << ") ---\n";
    int correctCount2 = 0;
    for (int i = 0; i < NUM_KEYS; ++i) {
        if (check_get(store, i, i * 10, "Phase 3")) {
            correctCount2++;
        }
    }
    std::cout << "Correct gets in Phase 3: " << correctCount2 << " / " << NUM_KEYS << "\n";
    std::cout << "BufferPool stats after Phase 3 (cumulative since Phase 2 reset):\n";
    std::cout << "  hits      = " << bp.hits   << "  (expect > 0, ideally many)\n";
    std::cout << "  misses    = " << bp.misses << "\n";
    std::cout << "  evictions = " << bp.evictions << "\n";

    // -----------------------
    // Phase 4: random access stress
    // -----------------------
    std::cout << "\n--- PHASE 4: random GET stress ---\n";
    bp.resetStats();
    srand(12345);
    const int RANDOM_QUERIES = 200;
    int correctRandom = 0;
    for (int i = 0; i < RANDOM_QUERIES; ++i) {
        int k = rand() % NUM_KEYS;
        if (check_get(store, k, k * 10, "Phase 4")) {
            correctRandom++;
        }
    }
    std::cout << "Correct random gets: " << correctRandom << " / " << RANDOM_QUERIES << "\n";
    std::cout << "BufferPool stats after Phase 4:\n";
    std::cout << "  hits      = " << bp.hits   << "  (expect many hits here)\n";
    std::cout << "  misses    = " << bp.misses << "\n";
    std::cout << "  evictions = " << bp.evictions << "\n";

    std::cout << "\n=== TEST COMPLETE ===\n";
    return 0;
}
