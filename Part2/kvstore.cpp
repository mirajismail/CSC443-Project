#include <string>
#include <vector>
#include <map>
#include <sys/stat.h>
#include "sstable.cpp"
#include "memtable.cpp"
#include "buffer_pool.hpp"

template <typename K, typename V>
class KVStore {
    std::string db_name_;
    size_t memtable_size_;
    std::vector<SSTable<K, V>> sstables_;
    MemTable<K, V> memtable_;
    BufferPool buffer_pool_;
    int sst_index_ = 0;

    SSTSearchMode sst_mode_ = SSTSearchMode::Binary;
    bool use_direct_ = true;

    void loadSSTables() {
        int index = 0;

        while (true) {
            std::string filename = db_name_ + "_" + std::to_string(index);
            std::ifstream infile(filename);
            if (!infile.is_open()) break;

            SSTable<K, V> sst{filename, memtable_size_, &buffer_pool_, sst_mode_, use_direct_};
            sstables_.push_back(std::move(sst));
            index++;
        }

        sst_index_ = index;
    }

public:
    KVStore(size_t memtable_size, size_t buffer_pool_frames)
        : memtable_size_(memtable_size),
          memtable_(memtable_size),
          buffer_pool_(buffer_pool_frames)
    {}

    void setSSTMode(SSTSearchMode mode) {
        sst_mode_ = mode;
    }

    void setUseDirect(bool use_direct) {
        use_direct_ = use_direct;
    }

    BufferPool& getBufferPool() {
        return buffer_pool_;
    }

    void open(const std::string& name) {
        db_name_ = name;
        struct stat st;

        if (stat(db_name_.c_str(), &st) != 0) {
            mkdir(db_name_.c_str(), 0755);
        }

        loadSSTables();
    }

    void close() {
        if (memtable_.size() > 0) {
            std::string filename = db_name_ + "_" + std::to_string(sst_index_);
            std::vector<std::pair<K,V>> pairs = memtable_.inorder();
            SSTable<K, V> sst{filename, memtable_size_, &buffer_pool_, sst_mode_, use_direct_};
            sst.writeFromPairs(pairs);
            sstables_.push_back(std::move(sst));
            memtable_.clear();
            sst_index_++;
        }

        sstables_.clear();
    }

    void put(const K& key, const V& value) {
        memtable_.put(key, value);

        if (memtable_.isFull()) {
            std::string filename = db_name_ + "_" + std::to_string(sst_index_);
            std::vector<std::pair<K,V>> pairs = memtable_.inorder();
            SSTable<K, V> sst{filename, memtable_size_, &buffer_pool_, sst_mode_, use_direct_};
            sst.writeFromPairs(pairs);
            sstables_.push_back(std::move(sst));
            memtable_.clear();
            sst_index_++;
        }
    }

    V* get(const K& key) {
        V* value = memtable_.get(key);
        if (value) return value;

        for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
            value = it->get(key);
            if (value) return value;
        }

        return nullptr;
    }

    std::vector<std::pair<K, V>> scan(K start_key, K end_key) {
        std::map<K, V> merged;

        // scan sstables oldest to newest so newer values overwrite
        for (auto& sst : sstables_) {
            auto pairs = sst.scan(start_key, end_key);
            for (const auto& [k, v] : pairs) {
                merged[k] = v;
            }
        }

        // scan memtable last (most recent)
        auto mem_pairs = memtable_.scan(start_key, end_key);
        for (const auto& [k, v] : mem_pairs) {
            merged[k] = v;
        }

        return std::vector<std::pair<K, V>>(merged.begin(), merged.end());
    }

    ~KVStore() {
        close();
    }
};
