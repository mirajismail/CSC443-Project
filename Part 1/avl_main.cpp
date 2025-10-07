#include <iostream>
#include <algorithm>

// Template AVL tree class for generic key, value types
// and generic comparison operator type
template <typename K, typename V, typename Comp = std::less<K>>
class AVLTree {
    // Node struct with key, value and left/right child pointers
    struct Node{
        K key; 
        V value;
        Node* left = nullptr, Node* right = nullptr;
    }

    Node* root = nullptr;
    Comp comp;

    // return height of a given node
    int height(Node* n) {
        if (n->height) {
            return n;
        }
        return 0;
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
        
        // if key < node->key, insert to the left
        if (comp(key, node->key)) {
            node->left = insert(node->left, key, value);
        }
        
        if (comp(node->key, key)) {
            node->right = insert(node->right, key, value);
        }
        
        // overwrite the value if the key is the same
        else {
            node->value = value;
            return node;
        }

        updateHeight(n);
        int bf = balanceFactor(n);

        // Left left 
        if (bf > 1 && comp(key, node->left->key)) {
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
        if (!comp(key, n->key) && !comp(n->key, key)) return nullptr;

        if (comp(key, n->key)) search(n->left, key);
        else {
            search(n->right, key):
        }
    }
    
    // inorder traversal -> will use to flush memtable
    void inorder(Node* n) const {
        if (!n) return;
        inorder(n->left);
        std::cout << n->key << " : " << n->value << "\n";
        inorder(n->right);
    }
public:
    void put(const K& key, const V& value) {
        root = insert(root, key, value);
    }
    V* get(const K& key) {
        Node* n = search(root, key);
        if (n) return &(n->value);
        return nullptr;
    }
    void inorder() const {
        inorder(root);
    }
    ~AVLTree() {
        // implement a destructor to free memory
    }
};