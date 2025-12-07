#include <memory>
#include "sst_base.hpp"
#include "binary_sst.hpp"
#include "btree_sst.hpp"

// SSTable wrapper that selects implementation based on search mode
template <typename K, typename V>
class SSTable {
    std::unique_ptr<SSTBase<K, V>> impl_;
    SSTSearchMode mode_;

public:
    // make_unique is used to create a unique_prt that is auto deleted when necessary

    SSTable(const std::string& filepath, size_t maxSize, BufferPool* bp,
            SSTSearchMode mode = SSTSearchMode::Binary, bool useDirect = false)
        : mode_(mode)
    {
        if (mode == SSTSearchMode::BTree) {
            impl_ = std::make_unique<BTreeSST<K, V>>(filepath, maxSize, bp, useDirect);
        } else {
            impl_ = std::make_unique<BinarySST<K, V>>(filepath, maxSize, bp, useDirect);
        }
    }

    SSTable(const std::string& filepath, size_t maxSize)
        : mode_(SSTSearchMode::Binary)
    {
        impl_ = std::make_unique<BinarySST<K, V>>(filepath, maxSize, nullptr, false);
    }

    void writeFromPairs(const std::vector<std::pair<K, V>>& entries) {
        impl_->writeFromPairs(entries);
    }

    V* get(const K& key) const {
        return impl_->get(key);
    }

    std::vector<std::pair<K, V>> scan(const K& start, const K& end) {
        return impl_->scan(start, end);
    }

    const std::string& path() const { return impl_->path(); }
};

