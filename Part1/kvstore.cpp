#include <string>
#include <vector>
#include "sstable.cpp"
#include "memtable.cpp"
#include <iostream>
#include "../Part2/buffer_pool.hpp"

template <typename K, typename V>
class KVStore {
    std::string dbName;
    size_t memTableSize;
    std::vector<SSTable<K, V>> sstables;
    MemTable<K, V> memTable;
    BufferPool bufferPool;
    int sstIndex = 0; // to track sst file naming

    // which SST search mode to use 
    SSTSearchMode sstMode = SSTSearchMode::Binary;

    void loadSSTables() {
        // load existing ssts from disk
        int index = 0;
        while (true) {
            std::string filename = dbName + "_" + std::to_string(index); // TODO: decide naming scheme
            std::ifstream infile(filename);
            if (!infile.is_open()) break; // no more ssts

            SSTable<K, V> sst{filename, memTableSize, &bufferPool, sstMode};
            sstables.push_back(std::move(sst)); // newest later
            index++;
        }
        sstIndex = index; // next sst index
    }

public:
    KVStore(size_t memTableSize, size_t bufferPoolFrames)
        : memTableSize(memTableSize),
          memTable(memTableSize),
          bufferPool(bufferPoolFrames)
    {}

    // choose algorithm for SSTs
    void setSSTMode(SSTSearchMode mode) {
        sstMode = mode;
    }

    BufferPool& getBufferPool() {
        return bufferPool;
    }

    void open(const std::string& name) {
        dbName = name;
        // TODO: write code to create db directory if not exists
        loadSSTables();
    }

    void close() {
        // TODO: closing code
        // flush memtable if not empty
        memTable.clear();
    }

    void put(const K& key, const V& value) {
        memTable.put(key, value);
        std::cout << "memtable size is " << memTable.size() << " after put" << std::endl;
        if (memTable.isFull()) {
            // flush memtable to disk
            std::string filename = dbName + "_" + std::to_string(sstIndex);
            std::vector<std::pair<K,V>> pairs = memTable.inorder();

            // create SST with the chosen mode
            SSTable<K, V> sst{filename, memTableSize, &bufferPool, sstMode};
            sst.writeFromPairs(pairs);
            sstables.push_back(std::move(sst));

            memTable.clear();
            sstIndex++;
        }
    }

    V* get(const K& key) {
        V* value = memTable.get(key);
        if (value) return value;
        std::cout << "checking ssts" << std::endl;
        for (auto it = sstables.rbegin(); it != sstables.rend(); ++it) {
            value = it->get(key);
            if (value) return value;
        }
        return nullptr;
    }

    // TODO: implement scan

    ~KVStore() {
        // TODO: write cleanup code
    }
};
