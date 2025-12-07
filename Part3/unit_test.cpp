// Unit tests for Part3 KVStore with buffer pool and LSM-tree
#include <iostream>
#include <string>
#include <cassert>
#include "kvstore.cpp" 

static constexpr size_t DEFAULT_BUFFER_FRAMES = 256;  // 1MB buffer pool

template<typename V>
bool checkValue(V* val, V expected) {
    return val && *val == expected;
}

void testBasicOperations() {
    std::cout << "\n=== basic put and get ===" << std::endl;
    KVStore<int, int> store{5, DEFAULT_BUFFER_FRAMES};
    store.open("test_basic");

    // insert some values
    store.put(1, 100);
    store.put(2, 200);
    store.put(3, 300);

    // verify they exist
    assert(checkValue(store.get(1), 100));
    assert(checkValue(store.get(2), 200));
    assert(checkValue(store.get(3), 300));
    
    // key that doesn't exist
    assert(store.get(99) == nullptr);

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testMemtableFlush() {
    std::cout << "\n=== memtable flush to sstable ===" << std::endl;
    KVStore<int, int> store{3, DEFAULT_BUFFER_FRAMES}; // small size to trigger flush
    store.open("test_flush");

    // fill memtable and trigger flush
    store.put(1, 10);
    store.put(2, 20);
    store.put(3, 30); // should trigger flush
    store.put(4, 40); // should go into new memtable

    // verify all values still accessible
    assert(checkValue(store.get(1), 10));
    assert(checkValue(store.get(2), 20));
    assert(checkValue(store.get(3), 30));
    assert(checkValue(store.get(4), 40));

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testUpdateValue() {
    std::cout << "\n=== update existing key ===" << std::endl;
    KVStore<int, int> store{5, DEFAULT_BUFFER_FRAMES};
    store.open("test_update");

    store.put(1, 100);
    assert(checkValue(store.get(1), 100));

    // update same key
    store.put(1, 999);
    assert(checkValue(store.get(1), 999));

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testScan() {
    std::cout << "\n=== scan range ===" << std::endl;
    KVStore<int, int> store{10, DEFAULT_BUFFER_FRAMES};
    store.open("test_scan");

    // insert values
    for (int i = 0; i < 10; i++) {
        store.put(i, i * 100);
    }

    // scan range [3, 7]
    auto results = store.scan(3, 7);
    assert(results.size() == 5);
    for (int i = 0; i < 5; i++) {
        assert(results[i].first == i + 3);
        assert(results[i].second == (i + 3) * 100);
    }

    // scan with no results
    auto empty = store.scan(100, 200);
    assert(empty.size() == 0);

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testScanAcrossSSTables() {
    std::cout << "\n=== scan across multiple sstables ===" << std::endl;
    KVStore<int, int> store{3, DEFAULT_BUFFER_FRAMES}; // small memtable to create multiple ssts
    store.open("test_scan_sst");

    // insert enough to create multiple sstables
    for (int i = 0; i < 10; i++) {
        store.put(i, i * 10);
    }

    // scan should merge results from memtable and sstables
    auto results = store.scan(2, 8);
    assert(results.size() == 7);
    for (size_t i = 0; i < results.size(); i++) {
        int expectedKey = i + 2;
        assert(results[i].first == expectedKey);
        assert(results[i].second == expectedKey * 10);
    }

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testMultipleSSTables() {
    std::cout << "\n=== multiple sstable lookups ===" << std::endl;
    KVStore<int, int> store{3, DEFAULT_BUFFER_FRAMES};
    store.open("test_multi_sst");

    // create multiple sstables
    for (int i = 0; i < 12; i++) {
        store.put(i, i * 5);
    }

    // verify all values (some in sstables, some in memtable)
    for (int i = 0; i < 12; i++) {
        assert(checkValue(store.get(i), i * 5));
    }

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testDelete() {
    std::cout << "\n=== delete key ===" << std::endl;
    KVStore<int, int> store{5, DEFAULT_BUFFER_FRAMES};
    store.open("test_delete");

    store.put(1, 100);
    store.put(2, 200);
    assert(checkValue(store.get(1), 100));

    // delete key
    store.del(1);
    assert(store.get(1) == nullptr);

    // other keys still exist
    assert(checkValue(store.get(2), 200));

    store.close();
    std::cout << "PASSED" << std::endl;
}

void testDeleteInScan() {
    std::cout << "\n=== deleted keys excluded from scan ===" << std::endl;
    KVStore<int, int> store{10, DEFAULT_BUFFER_FRAMES};
    store.open("test_delete_scan");

    // insert values
    for (int i = 0; i < 10; i++) {
        store.put(i, i * 100);
    }

    // delete some keys
    store.del(3);
    store.del(5);
    store.del(7);

    // scan should exclude deleted keys
    auto results = store.scan(0, 9);
    assert(results.size() == 7);  // 10 - 3 deleted = 7

    // verify deleted keys not in results
    for (const auto& [k, v] : results) {
        assert(k != 3 && k != 5 && k != 7);
    }

    store.close();
    std::cout << "PASSED" << std::endl;
}

void cleanup() {
    // remove test files and directories
    system("rm -rf test_basic* test_flush* test_update* test_scan* test_multi_sst* test_delete*");
}

int main() {
    cleanup(); // clean up any previous test files

    testBasicOperations();
    testMemtableFlush();
    testUpdateValue();
    testScan();
    testScanAcrossSSTables();
    testMultipleSSTables();
    testDelete();
    testDeleteInScan();

    cleanup();
    std::cout << "\n=== all tests passed ===" << std::endl;
    return 0;
}
