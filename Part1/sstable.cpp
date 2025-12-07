#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>

template <typename K, typename V>
class SSTable {
    std::string filepath_;
    int fd_;
    size_t num_entries_;
    static constexpr size_t pair_size = sizeof(K) + sizeof(V);
    
    std::pair<K, V> readPair(size_t index) {
        off_t offset = static_cast<off_t>(index * pair_size);
        char buffer[pair_size];

        ssize_t bytes = pread(fd_, buffer, pair_size, offset);
        if (bytes != static_cast<ssize_t>(pair_size))
            throw std::runtime_error("pread failed for entry at index " + std::to_string(index) + " in " + filepath_);

        K key;
        V value;
        std::memcpy(&key, buffer, sizeof(K));
        std::memcpy(&value, buffer + sizeof(K), sizeof(V));
        return {key, value};
    }

public:
    SSTable(const std::string& filepath, size_t) : filepath_(filepath), num_entries_(0) {
        fd_ = open(filepath.c_str(), O_RDONLY);

        if (fd_ != -1) {
            off_t file_size = lseek(fd_, 0, SEEK_END);
            num_entries_ = file_size / pair_size;
        }
    }

    void writeFromPairs(std::vector<std::pair<K, V>>& entries) {
        std::ofstream file(filepath_, std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open " + filepath_ + " for writing");
        }

        // cast to const char* for binary write of raw bytes
        for (const auto& [key, value] : entries) {
            file.write(reinterpret_cast<const char*>(&key), sizeof(K));
            file.write(reinterpret_cast<const char*>(&value), sizeof(V));
        }
        file.close();

        fd_ = open(filepath_.c_str(), O_RDONLY);
        if (fd_ == -1) {
            throw std::runtime_error("Could not reopen " + filepath_ + " for reading");
        }
        num_entries_ = entries.size();
    }

    V* get(const K& key) {
        if (fd_ == -1)
            return nullptr;

        size_t left = 0;
        size_t right = num_entries_;

        while (left < right) {
            size_t mid = (left + right) / 2;
            auto [mid_key, mid_val] = readPair(mid);

            if (mid_key == key)
                return new V(mid_val);
            if (mid_key < key)
                left = mid + 1;
            else
                right = mid;
        }
        return nullptr;
    }

    std::vector<std::pair<K, V>> scan(const K& start, const K& end) {
        std::vector<std::pair<K, V>> result;
        if (fd_ == -1)
            return result;

        // binary search to find starting index
        size_t left = 0;
        size_t right = num_entries_;
        while (left < right) {
            size_t mid = (left + right) / 2;
            auto [mid_key, unused] = readPair(mid);
            (void)unused;
            if (mid_key < start)
                left = mid + 1;
            else
                right = mid;
        }

        // sequential reads until out of range
        for (size_t i = left; i < num_entries_; ++i) {
            auto [key, value] = readPair(i);
            if (key > end)
                break;
            result.push_back({key, value});
        }

        return result;
    }

    void close() {
        if (fd_ != -1) {
            ::close(fd_); // use syscall close to not overlap
            fd_ = -1;
        }
    }

    const std::string& path() { return filepath_; }
};

