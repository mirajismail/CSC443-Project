#include <string>
#include <vector>
#include "sstable.cpp"
#include "memtable.cpp"

template <typename K, typename V>
class KVStore {
    std::string dbName;
    size_t memTableSize;
    std::vector<SSTable<K, V>> sstables;
    MemTable<K, V> memTable;
    int sstIndex = 0; // to track sst file naming

    void loadSSTables() {
        // load existing ssts from disk
        int index = 0;
        while (true) {
            std::string filename = dbName + "_" + std::to_string(index); // TODO: decide naming scheme
            std::ifstream infile(filename);
            if (!infile.is_open()) break; // no more ssts
            SSTable<K, V> sst {filename, memTableSize};
            sstables.push_back(sst); // TODO: decide to push or insert at front
            index++;
        }
        sstIndex = index; // next sst index
    }

    public:
        KVStore(size_t memTableSize) : memTableSize(memTableSize), memTable(memTableSize) {}

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
            memTable.put(key, value); // TODO: decide to check first or after
            if (memTable.isFull()) {
                // flush memtable to disk
                std::string filename = dbName + "_" + std::to_string(sstIndex); // TODO: decide naming convention
                std::vector<std::pair<K,V>> pairs = memTable.inorder();
                SSTable<K, V> sst {filename, memTableSize};
                sst.writeFromPairs(pairs);
                sstables.push_back(sst); // TODO: decide to push or insert at front
                memTable.clear();
                sstIndex++;
            }
        }

        V* get(const K& key) {
            V* value = memTable.get(key);
            if (value) return value;
            for (auto it = sstables.rbegin(); it != sstables.rend(); ++it) { // search from newest at the end, to oldest at the front
                value = it->get(key);
                if (value) return value;
            }
            return nullptr; // consider throwing exception
        }

        // TODO: implement scan
        
        ~KVStore() {
            // TODO: write cleanup code
        }

};

