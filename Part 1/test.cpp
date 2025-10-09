#include <iostream>
#include <string>
#include "kvstore.cpp" // Update this include if your header is named differently

int main() {
    // Instantiate kvstore<int, int>
    size_t size = 10;
    KVStore<int, int> store {size};

    // Test open
    store.open("teststore");

    // Test put
    store.put(1, 100);
    store.put(2, 200);

    // Test get
    int* value;
    value = store.get(1);
    if (value) {
        std::cout << "Key 1 found, value: " << *value << std::endl;
    } else {
        std::cout << "Key 1 not found." << std::endl;
    }

    value = store.get(2);
    if (value) {
        std::cout << "Key 2 found, value: " << *value << std::endl;
    } else {
        std::cout << "Key 2 not found." << std::endl;
    }

    value = store.get(3);
    if (value) {
        std::cout << "Key 3 found, value: " << *value << std::endl;
    } else {
        std::cout << "Key 3 not found." << std::endl;
    }

    return 0;
}
