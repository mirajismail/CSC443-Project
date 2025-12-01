#include <algorithm>
#include <vector>
#include <functional>

template <typename K, typename V, typename Comp = std::less<K>>
class AVLTree {
    struct Node {
        K key;
        V value;
        Node* left = nullptr;
        Node* right = nullptr;
        int height = 0;
    };

    Node* root = nullptr;
    Comp comp;

    int height(Node* n) {
        return n ? n->height : 0;
    }

    void updateHeight(Node* n) {
        n->height = 1 + std::max(height(n->left), height(n->right));
    }

    int balanceFactor(Node* n) {
        return n ? height(n->left) - height(n->right) : 0;
    }

    Node* rightRotate(Node* y) {
        Node* x = y->left;
        Node* T2 = x->right;

        x->right = y;
        y->left = T2;

        updateHeight(y);
        updateHeight(x);
        return x;
    }

    Node* leftRotate(Node* x) {
        Node* y = x->right;
        Node* T2 = y->left;

        y->left = x;
        x->right = T2;

        updateHeight(x);
        updateHeight(y);
        return y;
    }
    
    Node* insert(Node* n, const K& key, const V& value) {
        if (!n) return new Node{key, value};

        if (comp(key, n->key)) {
            n->left = insert(n->left, key, value);
        } else if (comp(n->key, key)) {
            n->right = insert(n->right, key, value);
        } else {
            n->value = value;
            return n;
        }

        updateHeight(n);
        int bf = balanceFactor(n);

        // Left-Left
        if (bf > 1 && comp(key, n->left->key))
            return rightRotate(n);

        // Right-Right
        if (bf < -1 && comp(n->right->key, key))
            return leftRotate(n);

        // Left-Right
        if (bf > 1 && comp(n->left->key, key)) {
            n->left = leftRotate(n->left);
            return rightRotate(n);
        }

        // Right-Left
        if (bf < -1 && comp(key, n->right->key)) {
            n->right = rightRotate(n->right);
            return leftRotate(n);
        }

        return n;
    }

    Node* search(Node* n, const K& key) {
        if (!n) return nullptr;
        if (!comp(key, n->key) && !comp(n->key, key)) return n;

        if (comp(key, n->key))
            return search(n->left, key);
        else
            return search(n->right, key);
    }

    void inorderHelper(Node* n, std::vector<std::pair<K, V>>& result) const {
        if (!n) return;
        inorderHelper(n->left, result);
        result.emplace_back(n->key, n->value);
        inorderHelper(n->right, result);
    }

    void deleteTree(Node* n) {
        if (!n) return;
        deleteTree(n->left);
        deleteTree(n->right);
        delete n;
    }

public:
    void put(const K& key, const V& value) {
        root = insert(root, key, value);
    }

    V* get(const K& key) {
        Node* n = search(root, key);
        return n ? &(n->value) : nullptr;
    }

    std::vector<std::pair<K, V>> inorder() const {
        std::vector<std::pair<K, V>> result;
        inorderHelper(root, result);
        return result;
    }

    void clear() {
        deleteTree(root);
        root = nullptr;
    }

    ~AVLTree() {
        clear();
    }
};
