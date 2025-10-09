#include "avltree.cpp"
#include <vector>

template <typename K, typename V>
class MemTable {
    // TODO: decide on naming convention
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
            tree_.put(key, value);
            ++currSize_; // TODO: consider how size is tracked, currently just counting entries
        }

        // TODO: should we be using const everywhere?
        V* get (const K& key) {
            return tree_.get(key);
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

        std::vector<std::pair<K, V>> inorder() {
            return tree_.inorder();
        }

        // TODO: decide if we want to just clear the tree or delete and recreate
        void clear() {
            tree_.~AVLTree();
            tree_ = AVLTree<K,V>();
            currSize_ = 0;
        }

        size_t size() const {
            return currSize_;
        }

        ~MemTable() {
            clear();
        }

};

