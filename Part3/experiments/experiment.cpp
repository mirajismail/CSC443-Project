// experiments for Part3: throughput measurements
// outputs CSV data for plotting

#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <fstream>
#include <cstdio>
#include "../kvstore.cpp"

using Clock = std::chrono::high_resolution_clock;

// experiment 1: binary vs btree throughput at increasing sizes
void experiment1() {
    std::cout << "=== Experiment 1: Binary vs B-tree ===\n";
    std::cout << "data_size,binary_throughput,btree_throughput\n";

    // data sizes: 50K to 1M entries, buffer pool = 256 frames = 1MB
    std::vector<int> sizes = {50000,  100000, 150000, 200000, 250000, 300000, 350000, 400000};
    constexpr int FRAMES = 256;  // 1MB buffer pool
    constexpr int QUERIES = 50000;

    std::mt19937 rng(42);

    for (int n : sizes) {
        // generate sorted data
        std::vector<std::pair<int,int>> data;
        data.reserve(n);
        for (int i = 0; i < n; i++) {
            data.push_back({i, i * 10});
        }

        // generate random queries
        std::vector<int> queries;
        queries.reserve(QUERIES);
        std::uniform_int_distribution<int> dist(0, n - 1);
        for (int i = 0; i < QUERIES; i++) {
            queries.push_back(dist(rng));
        }

        double binary_qps = 0;
        double btree_qps = 0;

        // binary search test
        {
            BufferPool pool(FRAMES);
            SSTable<int,int> sst("exp1_binary", n, &pool, SSTSearchMode::Binary);
            sst.writeFromPairs(data);
            pool.resetStats();

            auto t0 = Clock::now();
            for (int key : queries) {
                int* v = sst.get(key);
                delete v;
            }
            auto t1 = Clock::now();
            double secs = std::chrono::duration<double>(t1 - t0).count();
            binary_qps = QUERIES / secs;

            std::remove("exp1_binary");
            std::remove("exp1_binary.bloom");
        }

        // btree test
        {
            BufferPool pool(FRAMES);
            SSTable<int,int> sst("exp1_btree", n, &pool, SSTSearchMode::BTree);
            sst.writeFromPairs(data);
            pool.resetStats();

            auto t0 = Clock::now();
            for (int key : queries) {
                int* v = sst.get(key);
                delete v;
            }
            auto t1 = Clock::now();
            double secs = std::chrono::duration<double>(t1 - t0).count();
            btree_qps = QUERIES / secs;

            std::remove("exp1_btree");
            std::remove("exp1_btree.bloom");
        }

        std::cout << n << "," << binary_qps << "," << btree_qps << "\n";
    }
}

// experiment 2: insert/get/scan throughput over 1GB insertion
void experiment2() {
    std::cout << "\n=== Experiment 2: Throughput over 1GB insertion ===\n";
    std::cout << "data_mb,insert_throughput,get_throughput,scan_throughput\n";

    // config per spec: 10MB buffer, 8 bits/entry bloom, 1MB memtable
    // entry size = 16 bytes (int64_t key + value)
    // 1GB = 1024MB = 67108864 entries at 16 bytes each

    constexpr size_t BUFFER_FRAMES = 2560;      // 10MB = 2560 * 4KB
    constexpr size_t MEMTABLE_ENTRIES = 65536;  // 1MB / 16 bytes = 65536 entries
    constexpr size_t TOTAL_ENTRIES = 67108864;  // 1GB of data
    constexpr size_t CHECKPOINT_MB = 100;       // measure every 100MB
    constexpr size_t ENTRIES_PER_CHECKPOINT = (CHECKPOINT_MB * 1024 * 1024) / 16;
    constexpr int GET_SAMPLES = 10000;
    constexpr int SCAN_SAMPLES = 100;
    constexpr int SCAN_RANGE = 1000;

    std::mt19937 rng(123);

    // cleanup old data
    std::system("rm -rf exp2_store exp2_store_*");

    KVStore<int64_t, int64_t> store(MEMTABLE_ENTRIES, BUFFER_FRAMES);
    store.open("exp2_store");

    size_t inserted = 0;
    size_t next_checkpoint = ENTRIES_PER_CHECKPOINT;

    // generate all keys upfront (shuffled for random insert order)
    std::vector<int64_t> keys(TOTAL_ENTRIES);
    for (size_t i = 0; i < TOTAL_ENTRIES; i++) {
        keys[i] = static_cast<int64_t>(i);
    }
    std::shuffle(keys.begin(), keys.end(), rng);

    // lambdas for measurements
    auto measure_get = [&]() {
        std::uniform_int_distribution<size_t> dist(0, inserted - 1);
        auto t0 = Clock::now();
        for (int i = 0; i < GET_SAMPLES; i++) {
            int64_t key = keys[dist(rng)];
            auto* v = store.get(key);
            (void)v;  // measuring lookup time
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        return GET_SAMPLES / secs;
    };

    auto measure_scan = [&]() {
        std::uniform_int_distribution<int64_t> dist(0, static_cast<int64_t>(inserted - SCAN_RANGE - 1));
        auto t0 = Clock::now();
        for (int i = 0; i < SCAN_SAMPLES; i++) {
            int64_t start = dist(rng);
            store.scan(start, start + SCAN_RANGE);
        }
        auto t1 = Clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        return (SCAN_SAMPLES * SCAN_RANGE) / secs;  // keys scanned per sec
    };

    auto insert_start = Clock::now();

    for (size_t i = 0; i < TOTAL_ENTRIES; i++) {
        store.put(keys[i], keys[i] * 10);
        inserted++;

        if (inserted >= next_checkpoint) {
            auto insert_end = Clock::now();
            double insert_secs = std::chrono::duration<double>(insert_end - insert_start).count();
            double insert_qps = ENTRIES_PER_CHECKPOINT / insert_secs;

            double get_qps = measure_get();
            double scan_qps = measure_scan();

            size_t mb = (inserted * 16) / (1024 * 1024);
            std::cout << mb << "," << insert_qps << "," << get_qps << "," << scan_qps << "\n";

            next_checkpoint += ENTRIES_PER_CHECKPOINT;
            insert_start = Clock::now();
        }
    }

    store.close();

    // cleanup
    std::system("rm -rf exp2_store exp2_store_*");
}

// use with argument 1 for experiment 1, 2 for experiment 2
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "incorrect # arguments\n";
        return 1;
    }

    std::string mode = argv[1];
    if (mode == "1") {
        experiment1();
    }
    if (mode == "2") {
        experiment2();
    }

    return 0;
}
