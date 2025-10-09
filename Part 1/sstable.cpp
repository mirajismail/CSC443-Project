#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

// TODO: consider fd management, should we keep it open or close after each operation?
// currently keeping it open for simplicity
template <typename K, typename V>
class SSTable {
    std::string filepath_;
    int fd_;
    size_t maxSize_; // number of entries when memtable flushes
    size_t pairSize_ = sizeof(K) + sizeof(V); // Assuming fixed-size K and V for simplicity
    
    std::pair<K, V> readPair(size_t index) const {
        off_t offset = static_cast<off_t>(index * pairSize_);
        char buffer[pairSize_];

        // TODO: think if optimizations needed
        ssize_t bytes = pread(fd_, buffer, pairSize_, offset);
        if (bytes != (ssize_t)pairSize_)
            throw std::runtime_error("pread failed for entry at index " + std::to_string(index) + " in " + filepath_);

        K key;
        V value;
        std::memcpy(&key, buffer, sizeof(K));
        std::memcpy(&value, buffer + sizeof(K), sizeof(V));
        // std::cout << "readPair read key: " << key << ", val " << value << " from " << filepath_ << std::endl; 
        return {key, value};
    }

public:
    SSTable(const std::string& filepath, size_t maxSize) : filepath_(filepath), maxSize_(maxSize) {
        fd_ = open(filepath.c_str(), O_RDONLY); // may not exist yet, giving -1, handled in get/scan
    }

    // Write SST from sorted Key-Value pairs to disk
    void writeFromPairs(const std::vector<std::pair<K, V>>& entries) {
        std::cout << "flushing ";
        for (const auto [k,v] : entries) {
            std::cout << "(" << k << "," << v << "), ";
        }
        std::cout << "to " << filepath_ << std::endl; 

        std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open " + filepath_ + " for writing");
        }
        for (const auto& [key, value] : entries) {
            file.write(reinterpret_cast<const char*>(&key), sizeof(K));
            file.write(reinterpret_cast<const char*>(&value), sizeof(V));
        }
        file.close();

        // reopen for reading
        fd_ = open(filepath_.c_str(), O_RDONLY); // TODO: check which flags are correct
        if (fd_ == -1) {
            throw std::runtime_error("Could not reopen " + filepath_ + " for reading after writing");
        }

    }

    V* get(const K& key) const {
        if (fd_ == -1)
            return nullptr;

        // left and right are indices of the entries
        size_t left = 0;
        size_t right = maxSize_;

        while (left < right) {
            size_t mid = (left + right) / 2;
            auto [midKey, midVal] = readPair(mid);

            if (midKey == key)
                return new V(midVal);
            if (midKey < key)
                left = mid + 1;
            else
                right = mid;
        }
        return nullptr;
    }

    std::vector<std::pair<K, V>> scan(const K& start, const K& end) {
        std::vector<std::pair<K, V>> result;
        if (fd_ == -1)
            return result; // return empty if file doesn't exist

        // Binary search to find starting index
        size_t left = 0;
        size_t right = maxSize_;
        while (left < right) {
            size_t mid = (left + right) / 2;
            auto [midKey, _] = readPair(mid); // TODO: would it be better to have readKey for this?
            if (midKey < start)
                left = mid + 1;
            else
                right = mid;
        }

        // Sequential reads until out of range
        for (size_t i = left; i < maxSize_; ++i) {
            std::pair<K, V> pair = readPair(i);
            if (pair[0] > end)
                break;
            result.push_back(pair);
        }

        return result;
    }

    const std::string& path() { return filepath_; }
};

