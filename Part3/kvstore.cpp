#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <queue>
#include <limits>
#include <sys/stat.h>
#include <dirent.h>
#include <regex>
#include "sstable.cpp"
#include "memtable.cpp"
#include "buffer_pool.hpp"

template <typename K, typename V>
class KVStore {
    std::string db_name_;
    size_t memtable_size_;
    
    // levels_[0]=L1, levels_[1]=L2, ... 
    std::vector<std::vector<SSTable<K, V>>> levels_;
    MemTable<K, V> memtable_;  // L0: in-memory
    BufferPool buffer_pool_;
    
    // track next SST index per disk level
    std::vector<int> sst_indices_;
    
    size_t level_ratio_ = 2;  // ratio between LSM levels
    static constexpr int MAX_DISK_LEVELS = 20;  // L1 through L20 

    SSTSearchMode sst_mode_ = SSTSearchMode::Binary;
    bool use_direct_ = true;

    // tombstone marker, INT64_MIN used to mark deleted values
    static constexpr V TOMBSTONE = std::numeric_limits<V>::min();

    bool isTombstone(const V& value) const {
        return value == TOMBSTONE;
    }

    // generate filename: dbname_L{disk_level}_{index}
    std::string sstFilename(int arr_idx, int index) {
        return db_name_ + "_L" + std::to_string(arr_idx + 1) + "_" + std::to_string(index);
    }

    void loadSSTables() {
        levels_.resize(MAX_DISK_LEVELS);
        sst_indices_.resize(MAX_DISK_LEVELS, 0);

        DIR* dir = opendir(db_name_.c_str());
        if (!dir) return;

        std::regex pattern(".*_L(\\d+)_(\\d+)$");
        struct dirent* entry;
        
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            std::smatch match;
            
            if (std::regex_match(filename, match, pattern)) {
                int disk_level = std::stoi(match[1]);  
                int index = std::stoi(match[2]);
                int arr_idx = disk_level - 1; 
                
                if (arr_idx >= 0 && arr_idx < MAX_DISK_LEVELS) {
                    std::string filepath = db_name_ + "/" + filename;
                    SSTable<K, V> sst{filepath, memtable_size_, &buffer_pool_, sst_mode_, use_direct_};
                    
                    // ensure vector is large enough
                    if (static_cast<size_t>(index) >= levels_[arr_idx].size()) {
                        levels_[arr_idx].resize(index + 1);
                    }
                    levels_[arr_idx][index] = std::move(sst);
                    
                    if (index >= sst_indices_[arr_idx]) {
                        sst_indices_[arr_idx] = index + 1;
                    }
                }
            }
        }
        closedir(dir);
    }

    // max SSTables allowed at each level before compaction triggers
    size_t levelCapacity(int arr_idx) {
        return std::pow(level_ratio_, arr_idx + 1);
    }

    // merge for compaction
    std::vector<std::pair<K, V>> mergeSort(std::vector<std::vector<std::pair<K, V>>>& inputs) {
        if (inputs.empty()) return {};
        
        // min-heap: (key, value, source_idx, pos_in_source)
        using Entry = std::tuple<K, V, size_t, size_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
 
        // initialize heap with first element from each input
        for (size_t i = 0; i < inputs.size(); i++) {
            if (!inputs[i].empty()) {
                heap.push({inputs[i][0].first, inputs[i][0].second, i, 0});
            }
        }
        
        std::vector<std::pair<K, V>> result;
        K last_key{};
        bool has_last = false;
        
        while (!heap.empty()) {
            auto [key, value, src, pos] = heap.top();
            heap.pop();
            
            // skip duplicates, keeping newest (theorerically useless)
            if (!has_last || key != last_key) {
                result.push_back({key, value});
                last_key = key;
                has_last = true;
            }
            
            // add next element from same source
            if (pos + 1 < inputs[src].size()) {
                heap.push({inputs[src][pos + 1].first, inputs[src][pos + 1].second, src, pos + 1});
            }
        }
        
        return result;
    }

public:
    KVStore(size_t memtable_size, size_t buffer_pool_frames)
        : memtable_size_(memtable_size),
          memtable_(memtable_size),
          buffer_pool_(buffer_pool_frames)
    {
        levels_.resize(MAX_DISK_LEVELS);
        sst_indices_.resize(MAX_DISK_LEVELS, 0);
    }

    // setters and getters

    void setSSTMode(SSTSearchMode mode) {
        sst_mode_ = mode;
    }

    void setUseDirect(bool use_direct) {
        use_direct_ = use_direct;
    }

    void open(const std::string& name) {
        db_name_ = name;
        struct stat st;

        // create db directory if doesn't exist
        if (stat(db_name_.c_str(), &st) != 0) {
            mkdir(db_name_.c_str(), 0755);
        }

        loadSSTables();
    }

    void close() {
        if (memtable_.size() > 0) {
            flushMemtable();
        }

        for (auto& level : levels_) {
            level.clear();
        }
    }

    // flush L0 (memtable) to L1 (first disk level)
    void flushMemtable() {
        std::string filename = db_name_ + "/" + sstFilename(0, sst_indices_[0]);  // L1
        std::vector<std::pair<K,V>> pairs = memtable_.inorder();
        SSTable<K, V> sst{filename, memtable_size_, &buffer_pool_, sst_mode_, use_direct_};
        sst.writeFromPairs(pairs);
        levels_[0].push_back(std::move(sst));  // L1 = levels_[0]
        memtable_.clear();
        sst_indices_[0]++;

        // trigger compaction if needed
        maybeCompact();
    }

    void put(const K& key, const V& value) {
        memtable_.put(key, value);

        if (memtable_.isFull()) {
            flushMemtable();
        }
    }

    // mark key as deleted
    void del(const K& key) {
        put(key, TOMBSTONE);
    }

    V* get(const K& key) {
        // check L0 (memtable) first - most recent
        V* value = memtable_.get(key);
        if (value) {
            return isTombstone(*value) ? nullptr : value;
        }

        // search disk levels L1 to Ln (newest to oldest)
        for (size_t arr_idx = 0; arr_idx < levels_.size(); arr_idx++) {
            auto& level_ssts = levels_[arr_idx];
            
            for (auto it = level_ssts.rbegin(); it != level_ssts.rend(); ++it) {
                value = it->get(key);
                if (value) {
                    return isTombstone(*value) ? nullptr : value;
                }
            }
        }

        return nullptr;
    }

    std::vector<std::pair<K, V>> scan(K start_key, K end_key) {
        std::map<K, V> merged;

        // scan disk levels oldest to newest so newer values overwrite
        for (int arr_idx = MAX_DISK_LEVELS - 1; arr_idx >= 0; arr_idx--) {
            // within each level, oldest to newest
            for (auto& sst : levels_[arr_idx]) {
                auto pairs = sst.scan(start_key, end_key);
                for (const auto& [k, v] : pairs) {
                    merged[k] = v;
                }
            }
        }

        // L0 (memtable) is most recent
        auto mem_pairs = memtable_.scan(start_key, end_key);
        for (const auto& [k, v] : mem_pairs) {
            merged[k] = v;
        }

        // filter out tombstones from result
        std::vector<std::pair<K, V>> result;
        for (const auto& [k, v] : merged) {
            if (!isTombstone(v)) {
                result.push_back({k, v});
            }
        }
        return result;
    }

    // for testing, returns count of SSTables at disk level
    size_t levelCount(int disk_level) const {
        int arr_idx = disk_level - 1;
        if (arr_idx < 0 || arr_idx >= MAX_DISK_LEVELS) return 0;
        return levels_[arr_idx].size();
    }

    // compact disk level into next level
    void compact(int arr_idx) {
        if (arr_idx < 0 || arr_idx >= MAX_DISK_LEVELS - 1) return;
        if (levels_[arr_idx].empty()) return;

        // collect all pairs from this level
        std::vector<std::vector<std::pair<K, V>>> sst_data;
        for (auto& sst : levels_[arr_idx]) {
            // scan entire range
            auto pairs = sst.scan(std::numeric_limits<K>::min(), 
                                  std::numeric_limits<K>::max());
            sst_data.push_back(std::move(pairs));
        }

        // merge all SSTables
        std::vector<std::pair<K, V>> merged = mergeSort(sst_data);

        // drop tombstones when compacting to last disk level
        bool is_last_level = (arr_idx + 1 == MAX_DISK_LEVELS - 1);
        if (is_last_level) {
            std::vector<std::pair<K, V>> filtered;
            for (const auto& [k, v] : merged) {
                if (!isTombstone(v)) {
                    filtered.push_back({k, v});
                }
            }
            merged = std::move(filtered);
        }

        // delete old SST files and their bloom filters
        for (size_t i = 0; i < levels_[arr_idx].size(); i++) {
            std::string filename = db_name_ + "/" + sstFilename(arr_idx, i);
            buffer_pool_.invalidateFile(filename);  // invalidate cached pages
            std::remove(filename.c_str());
            std::remove((filename + ".bloom").c_str());
        }
        levels_[arr_idx].clear();
        sst_indices_[arr_idx] = 0;

        // write merged data to next level
        if (!merged.empty()) {
            int next_idx = arr_idx + 1;
            std::string filename = db_name_ + "/" + sstFilename(next_idx, sst_indices_[next_idx]);
            SSTable<K, V> sst{filename, memtable_size_, &buffer_pool_, sst_mode_, use_direct_};
            sst.writeFromPairs(merged);
            levels_[next_idx].push_back(std::move(sst));
            sst_indices_[next_idx]++;
        }
    }

    // trigger compaction if any disk level exceeds capacity
    void maybeCompact() {
        for (int arr_idx = 0; arr_idx < MAX_DISK_LEVELS - 1; arr_idx++) {
            if (levels_[arr_idx].size() > levelCapacity(arr_idx)) {
                compact(arr_idx);
            }
        }
    }

    ~KVStore() {
        close();
    }
};
