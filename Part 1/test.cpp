#include <iostream>
#include <string>
#include "kvstore.cpp" // Update this include if your header is named differently

int main() {
    // Instantiate kvstore<int, int>
    size_t size = 5;
    KVStore<int, int> store {size};

    // Test open
    store.open("teststore");
    std::cout << "opened" << std::endl;

    for (int i = 0; i < 9; i++) {
        store.put(i, i*100);
    }

    int* val;
    for (int i = 0; i < 10; i++) {
        val = store.get(i);
        if (val) {
            std::cout << "Key " << i << " found, value: " << *val << std::endl;
        } else {
            std::cout << "Key " << i << " not found." << std::endl;
        }
    }

    // Does not handle duplicate values
    store.put(0, 100);
    val = store.get(0);
    if (val) {
        std::cout << "Key " << 0 << " found, value: " << *val << std::endl;
    } else {
        std::cout << "Key " << 0 << " not found." << std::endl;
    }

    val = store.get(5);
    if (val) {
        std::cout << "Key " << 5 << " found, value: " << *val << std::endl;
    } else {
        std::cout << "Key " << 5 << " not found." << std::endl;
    }

    return 0;
}
