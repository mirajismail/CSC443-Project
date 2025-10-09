#include <algorithm>
#include <vector>
#include <functional>
#include <iostream>

// Template AVL tree class for generic key, value types
// and generic comparison operator type
template <typename K, typename V, typename Comp = std::less<K>>
class AVLTree {
    // Node struct with key, value and left/right child pointers
    struct Node{
        K key; 
        V value;
        Node* left = nullptr;
        Node* right = nullptr;
        int height = 0;
    };

    Node* root = nullptr;
    Comp comp;

    // return height of a given node
    int height(Node* n) {
        if (!n) return 0;
        return n->height;
    }

    // update the height of a given node based off height of its children
    void updateHeight(Node* n) {
        n->height = 1 + std::max(height(n->left), height(n->right));
    }
    
    // return balance factor of given node
    int balanceFactor(Node* n) {
        if (n) {
            // the balance factor is
            // the height of the left child minus the height of the right child
            return height(n->left) - height(n->right);
        }
        return 0;
    }

    // right rotate
    // ** description of right rotation **
    Node* rightRotate(Node* y) {
        Node* x = y->left;
        Node* T2 = x->right;
        
        x->right = y;
        y->left = T2;

        updateHeight(y);
        updateHeight(x);

        return x;
    }

    // left rotate
    // ** description of left rotation **
    Node* leftRotate(Node* x) {
        Node* y = x->right;
        Node* T2 = y->left;

        y->left = x;
        x->right = T2;

        updateHeight(x);
        updateHeight(y);
        return y;
    }
    
    // insert a new node - we will use this method for implementing "put"
    Node* insert(Node* n, const K& key, const V& value) {
        if (!n) return new Node{key, value};
        
        // if key < n->key, insert to the left
        if (comp(key, n->key)) {
            n->left = insert(n->left, key, value);
        }
        
        if (comp(n->key, key)) {
            n->right = insert(n->right, key, value);
        }
        
        // overwrite the value if the key is the same
        else {
            n->value = value;
            return n;
        }

        updateHeight(n);
        int bf = balanceFactor(n);

        // Left left 
        if (bf > 1 && comp(key, n->left->key)) {
            return rightRotate(n);
        }

        // Right right
        if (bf < -1 && comp(n->right->key, key)) {
            return leftRotate(n);
        }

        // Left right
        if (bf > 1 && comp(n->left->key, key)) {
                n->left = leftRotate(n->left);
                return rightRotate(n);
        }

        // Right left
        if (bf < -1 && comp(key, n->right->key)) {
            n->right = rightRotate(n->right);
            return leftRotate(n);
        }

        return n;
    }

    // search for a node
    Node* search(Node* n, const K& key) {
        if (!n) return nullptr;
        if (!comp(key, n->key) && !comp(n->key, key)) return n;

        if (comp(key, n->key)) search(n->left, key);
        else {
            search(n->right, key);
        }
    }
    
    // inorder traversal -> will use to flush memtable
    // helper for recursion
    void inorderHelper(Node* n, std::vector<std::pair<K, V>>& result) {
        if (!n) return;
        inorderHelper(n->left, result);
        result.emplace_back(n->key, n->value); // emplace back to avoid making copy
        inorderHelper(n->right, result);
    }
public:
    void put(const K& key, const V& value) {
        root = insert(root, key, value);
        // std::cout << "root - key: " << root->key << ", val: " << root->value << std::endl;
    }
    V* get(const K& key) {
        Node* n = search(root, key);
        // if (n) std::cout << "get - key: " << n->key << ", val: " << n->value << std::endl;
        // else std::cout << "couldn't find key " << key << std::endl;
        if (n) return &(n->value);
        return nullptr;
    }
    std::vector<std::pair<K, V>> inorder() {
        std::vector<std::pair<K, V>> result;
        inorderHelper(root, result);
        return result;
    }
    ~AVLTree() {
        // implement a destructor to free memory
    }
};
