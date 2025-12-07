#include "avltree.cpp"
#include <vector>
#include <stdexcept>

template <typename K, typename V>
class MemTable {
    AVLTree<K, V> tree_;
    size_t curr_size_;
    size_t max_size_;

public:
    MemTable(size_t max_size) : curr_size_(0), max_size_(max_size) {}

    bool isFull() const {
        return curr_size_ >= max_size_;
    }

    void put(const K& key, const V& value) {
        if (isFull()) {
            throw std::runtime_error("MemTable is full");
        }
        tree_.put(key, value);
        curr_size_++; // TODO: consider how size is tracked, currently just counting entries
    }

    V* get(const K& key) {
        return tree_.get(key);
    }

    std::vector<std::pair<K, V>> scan(const K& start_key, const K& end_key) {
        std::vector<std::pair<K, V>> result;
        auto entries = tree_.inorder();

        for (const auto& [k, v] : entries) {
            if (k >= start_key && k <= end_key)
                result.push_back({k, v});
        }
        return result;
    }

    std::vector<std::pair<K, V>> inorder() {
        return tree_.inorder();
    }

    void clear() {
        tree_.clear();
        curr_size_ = 0;
    }

    size_t size() const {
        return curr_size_;
    }

};

