#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "xxhash.h"

// bloom filter for membership testing
class BloomFilter {
    std::vector<uint8_t> bits_;
    size_t num_bits_;
    size_t num_hashes_;

    void setBit(size_t idx) {
        bits_[idx / 8] |= (1 << (idx % 8));
    }

    bool getBit(size_t idx) const {
        return bits_[idx / 8] & (1 << (idx % 8));
    }

    // use xxhash with different seeds for k hash functions
    size_t hash(const void* key, size_t len, uint32_t seed) const {
        return XXH64(key, len, seed) % num_bits_;
    }

public:
    // construct with expected items and false positive rate
    BloomFilter(size_t expected_items, double fp_rate = 0.01) {
        // m = -n*ln(p) / (ln(2)^2)
        num_bits_ = static_cast<size_t>(
            -static_cast<double>(expected_items) * std::log(fp_rate) / (std::log(2) * std::log(2))
        );
        num_bits_ = std::max(num_bits_, size_t(64));  // minimum 64 bits
        
        // k = (m/n) * ln(2)
        num_hashes_ = static_cast<size_t>(
            (static_cast<double>(num_bits_) / expected_items) * std::log(2)
        );
        num_hashes_ = std::max(num_hashes_, size_t(1));
        
        bits_.resize((num_bits_ + 7) / 8, 0);
    }

    // construct with fixed bits per entry (for experiments)
    BloomFilter(size_t expected_items, size_t bits_per_entry) {
        num_bits_ = expected_items * bits_per_entry;
        num_bits_ = std::max(num_bits_, size_t(64));
        num_hashes_ = static_cast<size_t>(bits_per_entry * std::log(2));
        num_hashes_ = std::max(num_hashes_, size_t(1));
        bits_.resize((num_bits_ + 7) / 8, 0);
    }

    // construct from data
    BloomFilter(const uint8_t* data, size_t data_len, size_t num_bits, size_t num_hashes)
        : num_bits_(num_bits), num_hashes_(num_hashes) {
        bits_.assign(data, data + data_len); // get bits from start to end of data
    }

    template <typename K>
    void add(const K& key) {
        for (size_t i = 0; i < num_hashes_; i++) {
            size_t idx = hash(&key, sizeof(K), i);
            setBit(idx);
        }
    }

    template <typename K>
    bool contains(const K& key) const {
        for (size_t i = 0; i < num_hashes_; i++) {
            size_t idx = hash(&key, sizeof(K), i);
            if (!getBit(idx)) return false;
        }
        return true;
    }

    // bloom filter file writing
    // format: [num_bits_: 8 bytes][num_hashes_: 8 bytes][bits_: variable]

    // size of file
    size_t bloomFileSize() const {
        return sizeof(num_bits_) + sizeof(num_hashes_) + bits_.size();
    }

    // write bloom filter
    void bloomWrite(uint8_t* out) const {
        memcpy(out, &num_bits_, sizeof(num_bits_));
        out += sizeof(num_bits_);
        memcpy(out, &num_hashes_, sizeof(num_hashes_));
        out += sizeof(num_hashes_);
        memcpy(out, bits_.data(), bits_.size());
    }

    // load bloom filter
    static BloomFilter bloomLoad(const uint8_t* data) {
        size_t num_bits, num_hashes;

        // read num_bits
        memcpy(&num_bits, data, sizeof(num_bits));
        data += sizeof(num_bits);

        // read num_hashes
        memcpy(&num_hashes, data, sizeof(num_hashes));
        data += sizeof(num_hashes);
        
        // read bit map
        size_t byte_len = (num_bits + 7) / 8;
        return BloomFilter(data, byte_len, num_bits, num_hashes);
    }

    size_t numBits() const { return num_bits_; }
    size_t numHashes() const { return num_hashes_; }
    const std::vector<uint8_t>& data() const { return bits_; }
};
