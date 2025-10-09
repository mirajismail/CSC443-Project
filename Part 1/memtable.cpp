#include "avl_main.cpp"
#include <vector>

template <typename K, typename V>
class MemTable {
    // TODO: decide if naming convention
    AVLTree<K, V> tree_;
    size_t currSize_;
    size_t maxSize_; 

    public:
        MemTable(size_t maxSize) : currSize_(0), maxSize_(maxSize) {}

        bool isFull() const {
            return currSize_ >= maxSize_;
        }

        void put (const K& key, const V& value) {
            if (isFull()) {
                throw std::runtime_error("MemTable is full");
            }
            tree_.insert(key, value);
            ++currSize_; // TODO: consider how size is tracked, currently just counting entries
        }

        // TODO: should we be using const everywhere?
        V* get (const K& key) const {
            return tree_.search(key);
        }

        std::vector<std::pair<K, V>> scan(K& startKey, K& endKey) const {
            std::vector<std::pair<K, V>> result;
            auto inorderEntries = tree_.inorder(); // get all key-value pairs in sorted order
            for (const auto& [k, v] : inorderEntries) {
                if (k >= startKey && k <= endKey)
                    result.push_back({k, v});
            }
            return result;
        }

        // TODO: decide if we want to just clear the tree or delete and recreate
        void clear() {
            tree_.clear();
            currSize_ = 0;
        }

        size_t size() const {
            return currSize_;
        }

        ~MemTable() {
            clear();
        }

};

