#include <iostream>
#include <chrono>
#include <random>
#include <string>
#include "kvstore.cpp"

using Clock = std::chrono::high_resolution_clock;

static constexpr int N = 200000; // number of entries in SST
static constexpr int Q = 50000; // number of random queries
static constexpr int FRAMES = 256; // buffer pool size

std::vector<std::pair<int,int>> makeData() {
    std::vector<std::pair<int,int>> v;
    v.reserve(N);
    for (int i = 0; i < N; i++)
        v.push_back({i, i * 10});
    return v;
}

std::vector<int> makeQueries() {
    std::vector<int> q;
    q.reserve(Q);
    std::mt19937 rng(123);
    std::uniform_int_distribution<int> dist(0, N-1);

    for (int i = 0; i < Q; i++)
        q.push_back(dist(rng));

    return q;
}

double runTest(SSTSearchMode mode) {
    std::string file = (mode == SSTSearchMode::Binary ?
                        "sst_binary" : "sst_btree");

    // prepare data
    auto data = makeData();
    BufferPool pool(FRAMES);

    SSTable<int,int> sst(file, N, &pool, mode);
    sst.writeFromPairs(data);

    auto queries = makeQueries();

    // reset pool counters
    pool.resetStats();

    auto t0 = Clock::now();

    int correct = 0;
    for (int key : queries) {
        int* v = sst.get(key);
        if (v && *v == key * 10) correct++;
        delete v;
    }

    auto t1 = Clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    std::cout << (mode == SSTSearchMode::Binary ? "[BINARY]" : "[B-TREE]") << "\n";
    std::cout << "Correct = " << correct << "/" << Q << "\n";
    std::cout << "Time    = " << secs << " sec\n";
    std::cout << "Hits    = " << pool.hits << "\n";
    std::cout << "Misses  = " << pool.misses << "\n";
    std::cout << "Evict   = " << pool.evictions << "\n\n";

    return secs;
}

int main() {
    std::cout << "=== SST Binary vs B-Tree Comparison ===\n";

    double t_binary = runTest(SSTSearchMode::Binary);
    double t_btree  = runTest(SSTSearchMode::BTree);

    std::cout << "Speedup (Binary / B-tree): "
              << (t_binary / t_btree) << "x\n";

    return 0;
}
