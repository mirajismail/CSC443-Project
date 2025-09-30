#include <iostream>
#include <algorithm>

struct Node {
    int data;
    int height;
    Node* left;
    Node* right;
};

Node* createNode(int data) {
    Node* n = new Node();
    n->data = data;
    n->height = 1;
    n->left = n->right = nullptr;
    return n;
}

int height(Node* n) { return n ? n->height : 0; }

void updateHeight(Node* n) {
    n->height = 1 + std::max(height(n->left), height(n->right));
}

int balanceFactor(Node* n) {
    return n ? height(n->left) - height(n->right) : 0;
}

// Right rotate (y becomes right child of x)
Node* rightRotate(Node* y) {
    Node* x  = y->left;
    Node* T2 = x->right;

    x->right = y;
    y->left  = T2;

    updateHeight(y);
    updateHeight(x);
    return x;
}

// Left rotate (x becomes left child of y)
Node* leftRotate(Node* x) {
    Node* y  = x->right;
    Node* T2 = y->left;

    y->left  = x;
    x->right = T2;

    updateHeight(x);
    updateHeight(y);
    return y;
}

Node* insert(Node* root, int data) {
    if (!root) return createNode(data);

    if (data < root->data)      root->left  = insert(root->left, data);
    else if (data > root->data) root->right = insert(root->right, data);
    else return root; // no duplicates

    updateHeight(root);
    int bf = balanceFactor(root);

    // LL
    if (bf > 1 && data < root->left->data)
        return rightRotate(root);
    // RR
    if (bf < -1 && data > root->right->data)
        return leftRotate(root);
    // LR
    if (bf > 1 && data > root->left->data) {
        root->left = leftRotate(root->left);
        return rightRotate(root);
    }
    // RL
    if (bf < -1 && data < root->right->data) {
        root->right = rightRotate(root->right);
        return leftRotate(root);
    }

    return root;
}


Node* minNode(Node* n) {
    while (n && n->left) n = n->left;
    return n;
}

Node* deleteNode(Node* root, int key) {
    if (!root) return nullptr;

    if (key < root->data) {
        root->left = deleteNode(root->left, key);
    } else if (key > root->data) {
        root->right = deleteNode(root->right, key);
    } else {
        // node to delete found
        if (!root->left || !root->right) {
            Node* child = root->left ? root->left : root->right;
            delete root;
            return child;
        } else {
            Node* succ = minNode(root->right);
            root->data = succ->data;
            root->right = deleteNode(root->right, succ->data);
        }
    }

    updateHeight(root);
    int bf = balanceFactor(root);

    // LL
    if (bf > 1 && balanceFactor(root->left) >= 0)
        return rightRotate(root);
    // LR
    if (bf > 1 && balanceFactor(root->left) < 0) {
        root->left = leftRotate(root->left);
        return rightRotate(root);
    }
    // RR
    if (bf < -1 && balanceFactor(root->right) <= 0)
        return leftRotate(root);
    // RL
    if (bf < -1 && balanceFactor(root->right) > 0) {
        root->right = rightRotate(root->right);
        return leftRotate(root);
    }

    return root;
}

Node* search(Node* root, int key) {
    if (root == nullptr) return nullptr;
    if (root->data ==key) return root;

    if (root->data < key) return search(root->right, key);
    return search(root->left, key);
}

int main() {
    Node* root = nullptr;

    // Insert some values
    root = insert(root, 10);
    root = insert(root, 20);
    root = insert(root, 30); // should trigger rotation
    root = insert(root, 40);
    root = insert(root, 50);
    root = insert(root, 25); // mixed case rotation

    // Search tests
    std::cout << "Search 20: " << (search(root, 20) ? "Found\n" : "Not Found\n");
    std::cout << "Search 99: " << (search(root, 99) ? "Found\n" : "Not Found\n");

    // Deletion tests
    root = deleteNode(root, 40); // delete leaf
    root = deleteNode(root, 30); // delete node with one child
    root = deleteNode(root, 10); // delete node with two children

    // Check search again
    std::cout << "Search 40 after delete: " << (search(root, 40) ? "Found\n" : "Not Found\n");
    std::cout << "Search 25: " << (search(root, 25) ? "Found\n" : "Not Found\n");

    // Insert sequence to trigger all rotations
    root = nullptr;
    root = insert(root, 30);
    root = insert(root, 20);
    root = insert(root, 10); // LL rotation
    root = insert(root, 40);
    root = insert(root, 50); // RR rotation
    root = insert(root, 35); // LR / RL rotations may occur

    return 0;
}

