# CSC443/CSC2525H Group Programming Project Report

## Key-Value Store Implementation

**Group Members:**
- Daniel Grishanov
- Miraj Ismail

**Repository:** https://github.com/mirajismail/CSC443-Project

---

## Table of Contents

1. [Design Elements](#design-elements)
   - [Step 1: Memtable and SSTs](#step-1-memtable-and-ssts)
   - [Step 2: Buffer Pool and Static B-Trees](#step-2-buffer-pool-and-static-b-trees)
   - [Step 3: LSM-Tree with Bloom Filters](#step-3-lsm-tree-with-bloom-filters)
2. [Project Status](#project-status)
3. [Experiments](#experiments)
4. [Testing](#testing)
5. [Compilation and Running Instructions](#compilation-and-running-instructions)

---



## Design Elements

Our implementation uses templates to support various key and value types, but we use it with integer types. The code is written in C++17. The system supports the specified KV-store API: open, close, put, get, scan, and delete.

### Step 1: Memtable and SSTs

All Step 1 implementation files are located in the `Part1/` directory.

#### KV-Store Get API

**Location:** `Part1/kvstore.cpp`, function `get()`

The get operation retrieves a value associated with a given key. Our implementation searches the memtable first, then searches SSTs from newest to oldest using a reverse iterator. This ordering ensures we return the most recent value for a key that may have been updated.

```cpp
V* get(const K& key) {
    V* value = memtable_.get(key);
    if (value) return value;

    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        value = it->get(key);
        if (value) return value;
    }

    return nullptr;
}
```

The get returns a pointer to the value if found, or nullptr if the key does not exist. Using `rbegin()` and `rend()` iterates from newest to oldest, allowing us to stop as soon as we find the key since any earlier occurrences would be outdated.

#### KV-Store Put API

**Location:** `Part1/kvstore.cpp`, function `put()`

The put operation stores a key-value pair in the database. We insert into the memtable and flush to disk when the memtable reaches capacity.

```cpp
void put(const K& key, const V& value) {
    memtable_.put(key, value);

    if (memtable_.isFull()) {
        std::string filename = db_name_ + "_" + std::to_string(sst_index_);
        std::vector<std::pair<K,V>> pairs = memtable_.inorder();
        SSTable<K, V> sst{filename, memtable_size_};
        sst.writeFromPairs(pairs);
        sstables_.push_back(sst);
        memtable_.clear();
        sst_index_++;
    }
}
```

When the memtable is full, we perform an in-order traversal to get sorted key-value pairs, write them to a new SST file with a sequentially incrementing index, add the SST to our list, and clear the memtable for new insertions.

#### KV-Store Scan API

**Location:** `Part1/kvstore.cpp`, function `scan()`

The scan operation retrieves all key-value pairs within a specified range [key1, key2] in sorted order. This requires merging results from the memtable and all SSTs.

```cpp
std::vector<std::pair<K, V>> scan(K start_key, K end_key) {
    std::map<K, V> merged;

    // scan sstables oldest to newest so newer values overwrite
    for (auto& sst : sstables_) {
        auto pairs = sst.scan(start_key, end_key);
        for (const auto& [k, v] : pairs) {
            merged[k] = v;
        }
    }

    // scan memtable last (most recent)
    auto mem_pairs = memtable_.scan(start_key, end_key);
    for (const auto& [k, v] : mem_pairs) {
        merged[k] = v;
    }

    return std::vector<std::pair<K, V>>(merged.begin(), merged.end());
}
```

We process sources from oldest to newest so that newer values automatically overwrite older ones for the same key. Using a std::map handles deduplication and maintains sorted order. 

#### In-Memory Memtable as Balanced Binary Tree

**Location:** `Part1/avltree.cpp`, `Part1/memtable.cpp`

We implemented the memtable as an AVL tree.

**Node Structure:**

The AVL tree is implemented as a templated class with a configurable comparator (defaults to using the standard less than comparator):

```cpp
template <typename K, typename V, typename Comp = std::less<K>>
class AVLTree {
    struct Node {
        K key;
        V value;
        Node* left = nullptr;
        Node* right = nullptr;
        int height = 0;
    };
    
    //...
};
```

It is implemented with the standard AVL tree operations: get, put, inorder.

**Memtable Wrapper:**

The `MemTable` class wraps the AVL tree and adds size tracking:

```cpp
template <typename K, typename V>
class MemTable {
    AVLTree<K, V> tree_;
    size_t curr_size_;
    size_t max_size_;

public:
    MemTable(size_t max_size) : curr_size_(0), max_size_(max_size) {}

    bool isFull() const { return curr_size_ >= max_size_; }

    void put(const K& key, const V& value) {
        if (isFull()) {
            throw std::runtime_error("MemTable is full");
        }
        tree_.put(key, value);
        curr_size_++;
    }

    V* get(const K& key) { return tree_.get(key); }
    
    std::vector<std::pair<K, V>> scan(const K& start_key, const K& end_key);
    std::vector<std::pair<K, V>> inorder() { return tree_.inorder(); }
    void clear();
    size_t size() const { return curr_size_; }
};
```

The `memtable_size` parameter controls the maximum number of entries before flushing to disk. The put operation throws an exception if called when full, so callers must check `isFull()` before inserting.

#### SSTs in Storage with Binary Search

**Location:** `Part1/sstable.cpp`

When the memtable reaches capacity, we flush it to disk as a Sorted String Table (SST). The SST stores key-value pairs in sorted order, enabling efficient binary search.

**File Format:**

Each SST file stores entries sequentially:
```
[key_0][value_0][key_1][value_1]...[key_n][value_n]
```

Each key and value is 8 bytes (for int64_t), so each entry occupies 16 bytes. SST files follow the naming convention `<db_name>_<sequence_number>` (without extension).

**Writing an SST:**

The `writeFromPairs` method takes a sorted vector of key-value pairs from the memtable's in-order traversal and writes them sequentially to a file using buffered I/O.

**Binary Search for Point Queries:**

For point queries, we implement binary search over the SST file. Since all entries have fixed size, we can compute the byte offset of any entry directly:

```cpp
offset = entry_index * ENTRY_SIZE  // ENTRY_SIZE = 16 bytes
```

We use `pread` for random access reads during binary search. The SSTable class has a helper method `readPair` that reads a key-value pair at a given index:

```cpp
std::pair<K, V> readPair(size_t index) {
    off_t offset = static_cast<off_t>(index * pair_size);
    char buffer[pair_size];

    ssize_t bytes = pread(fd_, buffer, pair_size, offset);
    if (bytes != static_cast<ssize_t>(pair_size))
        throw std::runtime_error("pread failed for entry at index " + std::to_string(index) + " in " + filepath_);

    K key;
    V value;
    std::memcpy(&key, buffer, sizeof(K));
    std::memcpy(&value, buffer + sizeof(K), sizeof(V));
    return {key, value};
}

V* get(const K& key) {
    if (fd_ == -1) 
        return nullptr;

    size_t left = 0;
    size_t right = num_entries_;

    while (left < right) {
        size_t mid = (left + right) / 2;
        auto [mid_key, mid_val] = readPair(mid);

        if (mid_key == key)
            return new V(mid_val);
        if (mid_key < key)
            left = mid + 1;
        else
            right = mid;
    }
    return nullptr;
}
```

**Range Scans:**

For scan operations, we use binary search to find the starting position (first key >= start_key), then read entries sequentially until we encounter a key > end_key.

#### Database Open and Close API

**Location:** `Part1/kvstore.cpp`, functions `open()` and `close()`

**Open:**

The open operation creates a directory with the database name if it does not exist. If the directory already exists, we scan it for existing SST files and load them. This allows the database to persist across restarts.

```cpp
void open(const std::string& name) {
    db_name_ = name;
    struct stat st;

    if (stat(db_name_.c_str(), &st) != 0) {
        mkdir(db_name_.c_str(), 0755);
    }

    loadSSTables();
}
```

The `loadSSTables()` helper scans the directory and loads existing SSTs:

```cpp
void loadSSTables() {
    int index = 0;

    while (true) {
        std::string filename = db_name_ + "_" + std::to_string(index);
        std::ifstream infile(filename);
        if (!infile.is_open()) break;
        SSTable<K, V> sst{filename, memtable_size_};
        sstables_.push_back(sst);
        index++;
    }

    sst_index_ = index;
}
```

SST files are loaded sequentially by index until no more files are found.

**Close:**

The close operation flushes the current memtable to an SST if it contains any data, then releases all resources. This ensures no data loss when shutting down.

```cpp
void close() {
    // flush memtable to disk if not empty
    if (memtable_.size() > 0) {
        std::string filename = db_name_ + "_" + std::to_string(sst_index_);
        std::vector<std::pair<K,V>> pairs = memtable_.inorder();
        SSTable<K, V> sst{filename, memtable_size_};
        sst.writeFromPairs(pairs);
        sstables_.push_back(sst);
        memtable_.clear();
        sst_index_++;
    }

    for (auto& sst : sstables_) {
        sst.close();
    }

    sstables_.clear();
}
```

The destructor also calls `close()` to ensure data is persisted even if the user forgets to call it explicitly.

### Step 2: Buffer Pool and Static B-Trees

All Step 2 implementation files are located in the `Part2/` directory.

#### Implementation of Buffer Pool as Hash Table with Collision Resolution

**Location:** `Part2/buffer_pool.hpp`, `Part2/buffer_pool.cpp`

The buffer pool caches frequently accessed pages in memory to reduce disk I/O. We implemented it as a hash table for O(1) expected lookup time.

**Page Identification:**

Each page is uniquely identified by the file path and byte offset (4KB aligned):

```cpp
struct PageId {
    std::string file;   // SST filename
    uint64_t offset;    // byte offset in the file (4kB aligned)

    bool operator==(const PageId& other) const {
        return offset == other.offset && file == other.file;
    }
};
```

**Frame Structure:**

A frame holds one 4KB page along with metadata:

```cpp
struct alignas(4096) Frame {
    PageId id;
    bool valid = false;
    bool referenced = false;
    char data[PAGE_SIZE];           // PAGE_SIZE = 4096

    // hash table chaining
    Frame* hashNext = nullptr;

    // clock circular list
    Frame* clockPrev = nullptr;
    Frame* clockNext = nullptr;
};
```

The entire Frame struct is aligned to 4KB boundaries using `alignas(4096)` to support O_DIRECT I/O.

**Hash Function:**

We use xxHash (XXH3_64bits) (included in `xxhash.c` and `xxhash.h`) for hashing. The hash is computed by concatenating the filename and offset bytes:

```cpp
std::size_t BufferPool::hash(const PageId& pid) const {
    // buffer format: [file bytes][offset bytes]
    std::string buf;
    buf.append(pid.file);
    buf.append(reinterpret_cast<const char*>(&pid.offset), sizeof(uint64_t));

    return XXH3_64bits(buf.data(), buf.size());
}

size_t BufferPool::bucketIndex(const PageId& pid) const {
    return hash(pid) % table.size();
}
```

**Collision Resolution:**

We use separate chaining with linked lists. The hash table has 2x the number of buckets as frames to keep the load factor low and minimize collisions. Each bucket is the head of a linked list of frames that hash to that bucket.

```cpp
BufferPool::Frame* BufferPool::lookup(const PageId& pid) {
    size_t idx = bucketIndex(pid);
    Frame* f = table[idx];
    while (f) {
        if (f->valid && f->id == pid) {
            hits++;
            return f;
        }
        f = f->hashNext;
    }
    return nullptr;
}
```

The lookup also increments the `hits` counter for statistics tracking.

#### Integration of Buffer Pool with Queries

**Location:** `Part2/kvstore.cpp`, `Part2/btree_sst.hpp`, `Part2/binary_sst.hpp`

The buffer pool is integrated into all query paths. Every page read goes through `bufferPool->getPage()`:

```cpp
const char* BufferPool::getPage(const PageId& pid) {
    // 1) lookup
    if (Frame* hit = lookup(pid)) {
        hit->referenced = true;
        return hit->data;
    }

    // 2) miss - need a frame
    misses++;
    Frame* f = nullptr;
    if (usedFrames < capacityFrames) {
        f = allocateNewFrame();
    } else {
        f = evictOne();
    }

    // 3) load from disk
    f = loadPageIntoFrame(f, pid);
    return f->data;
}

// invalidate all cached pages for a file
void BufferPool::invalidateFile(const std::string& file) {
    for (size_t i = 0; i < usedFrames; i++) {
        Frame* f = &frames[i];
        if (f->valid && f->id.file == file) {
            removeFromHash(f);
            f->valid = false;
        }
    }
}
```


The SST base class provides a `getPageAt` method that routes through the buffer pool if available:

```cpp
const char* getPageAt(uint64_t pageOffset) const {
    if (bufferPool_) {
        return bufferPool_->getPage(PageId{filepath_, pageOffset});
    }
    return loadPageOS(pageOffset);  // fallback to direct pread
}
```

We also support O_DIRECT I/O to bypass the OS page cache, preventing double-buffering. This is controlled by the `useDirect` parameter when creating SSTs.

#### Clock Eviction Policy

**Location:** `Part2/buffer_pool.cpp`

We implemented the Clock algorithm for page replacement. It approximates LRU behavior with O(1) overhead per access.

**Data Structure:**

All frames are organized in a circular doubly-linked list. A clock hand pointer tracks the current position.

**Algorithm:**

Each frame has a referenced bit that is set whenever the page is accessed. When eviction is needed:

1. Starting from the clock hand position, examine each frame
2. If the referenced bit is 1, clear it to 0 (give a second chance) and go to next
3. If the referenced bit is 0, evict the frame

```cpp
BufferPool::Frame* BufferPool::evictOne() {
    if (!clockCur) {
        throw std::runtime_error("BufferPool::evictOne called with empty clock list");
    }

    while (true) {
        Frame* f = clockCur;
        if (!f->valid) {
            clockCur = clockCur->clockNext;
            continue;
        }

        if (f->referenced) {
            f->referenced = false;
            clockCur = clockCur->clockNext;
        } 
        else {
            // victim found
            evictions++;            
            removeFromHash(f);
            f->valid = false;
            f->referenced = false;
            clockCur = clockCur->clockNext;
            return f;
        }
    }
}
```

The Clock algorithm gives recently-accessed pages a second chance before eviction, approximating LRU without maintaining strict ordering, as discussed in class. The buffer pool tracks statistics (`hits`, `misses`, `evictions`) for performance analysis and testing.

#### Static B-Tree for SSTs

**Location:** `Part2/btree_sst.hpp`

We transform the flat SST structure into a static B-tree to reduce query I/O from O(log2(N/B)) to O(logB(N)), where B is the branching factor.

**Page Layout:**

Each 4096-byte page has an 8-byte header:
```
[nodeType: 1 byte][pad: 3 bytes][numKeys: 4 bytes][keys...][children/values...]
```

The `nodeType` field indicates:
- 1 = internal node
- 2 = leaf node

For leaf pages:
- Keys and values stored sequentially after header
- Maximum entries per leaf: `(page_size - header_size) / pair_size = (4096 - 8) / 16 = 255`

For internal pages:
- Keys stored first, followed by child page offsets (8 bytes each)
- Maximum keys per internal node calculated as: `(page_size - header_size - child_pointer_size) / (key_size + child_pointer_size) = (4096 - 8 - 8) / (8 + 8) = 254`
- Number of children = num_keys + 1

**Building the B-Tree (Bottom-Up):**

When creating an SST from sorted data, we build the B-tree bottom-up in a single pass:

1. Create leaf pages by filling each with as many entries as will fit
2. For each group of leaves, create a parent internal node with delimiter keys and child pointers
3. Repeat step 2 for internal nodes until we have a single root
4. Swap the root to page 0 so queries always start at offset 0

**B-Tree Search:**

Point queries traverse from root to leaf, using binary search within each node:

```cpp
V* searchBTree(const K& key) const {
    if (fd_ == -1) return nullptr;

    uint64_t offset = 0;

    while (true) {
        const char* page = getPageAt(offset);

        uint8_t nodeType = 0;
        std::memcpy(&nodeType, page, sizeof(uint8_t));

        uint32_t numKeys = 0;
        std::memcpy(&numKeys, page + 4, sizeof(uint32_t));

        if (nodeType == 2) { // leaf
            size_t left = 0, right = numKeys;

            while (left < right) {
                size_t mid = (left + right) / 2;
                size_t entryOffset = BTREE_HDR_SIZE + mid * pairSize_;

                K midKey;
                std::memcpy(&midKey, page + entryOffset, sizeof(K));
                if (midKey == key) {
                    V value;
                    std::memcpy(&value, page + entryOffset + sizeof(K), sizeof(V));
                    return new V(value);
                }
                if (midKey < key) {
                    left = mid + 1;
                }
                else {
                    right = mid;
                }
            }
            return nullptr;
        }
        else if (nodeType == 1) { // internal
            size_t keyStart = BTREE_HDR_SIZE;
            size_t childStart = BTREE_HDR_SIZE + (size_t)numKeys * sizeof(K);

            size_t lo = 0, hi = numKeys;
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                K delim;
                std::memcpy(&delim, page + keyStart + mid * sizeof(K), sizeof(K));
                if (key < delim) {
                    hi = mid;
                }
                else {
                    lo = mid + 1;
                }
            }
            size_t childIndex = lo;
            uint64_t childOffset = 0;
            std::memcpy(&childOffset,
                        page + childStart + childIndex * sizeof(uint64_t),
                        sizeof(uint64_t));
            offset = childOffset;
            continue;
        }
        else {
            throw std::runtime_error("Unknown B-tree node type in " + filepath_);
        }
    }
}
```

Each level requires one page access. 

**Scan Implementation:**

For range scans, we navigate to the leftmost relevant leaf, then scan sequentially across leaf pages until we exit the range.

### Step 3: LSM-Tree with Bloom Filters

All Step 3 implementation files are located in the `Part3/` directory.

#### Filter for SST and Integration with Get

**Location:** `Part3/bloom_filter.hpp`, `Part3/kvstore.cpp`

We used bloom filters.

**Data Structure:**

Uses bit operations to set and get correct bits. Uses XXHash, as mentioned for part 2.

```cpp
class BloomFilter {
    std::vector<uint8_t> bits_;
    size_t num_bits_;
    size_t num_hashes_;

    void setBit(size_t idx) {
        bits_[idx / 8] |= (1 << (idx % 8));
    }

    bool getBit(size_t idx) const {
        return bits_[idx / 8] & (1 << (idx % 8));
    }

    size_t hash(const void* key, size_t len, uint32_t seed) const {
        return XXH64(key, len, seed) % num_bits_;
    }
    // ...
};
```

**Constructors:**

The Bloom filter supports two construction modes:

1. With a target false positive rate (uses optimal parameters, as derived in class):
```cpp
BloomFilter(size_t expected_items, double fp_rate = 0.01) {
    // m = -n*ln(p) / (ln(2)^2)
    num_bits_ = static_cast<size_t>(
        -static_cast<double>(expected_items) * std::log(fp_rate) / (std::log(2) * std::log(2))
    );
    num_bits_ = std::max(num_bits_, size_t(64));  // minimum 64 bits
    
    // k = (m/n) * ln(2)
    num_hashes_ = static_cast<size_t>(
        (static_cast<double>(num_bits_) / expected_items) * std::log(2)
    );
    num_hashes_ = std::max(num_hashes_, size_t(1));
    
    bits_.resize((num_bits_ + 7) / 8, 0);
}
```

2. With fixed bits per entry (for experiments with consistent configuration):
```cpp
BloomFilter(size_t expected_items, size_t bits_per_entry) {
    num_bits_ = expected_items * bits_per_entry;
    num_bits_ = std::max(num_bits_, size_t(64));
    num_hashes_ = static_cast<size_t>(bits_per_entry * std::log(2));
    num_hashes_ = std::max(num_hashes_, size_t(1));
    bits_.resize((num_bits_ + 7) / 8, 0);
}
```

**Hash Functions:**

We use k independent hash functions implemented as XXH64 with different seeds (0, 1, 2, ... k-1):

```cpp
size_t hash(const void* key, size_t len, uint32_t seed) const {
    return XXH64(key, len, seed) % num_bits_;
}
```

**Operations:**

The add and contains methods are templated to work with any key type:

```cpp
template <typename K>
void add(const K& key) {
    for (size_t i = 0; i < num_hashes_; i++) {
        size_t idx = hash(&key, sizeof(K), i);
        setBit(idx);
    }
}

template <typename K>
bool contains(const K& key) const {
    for (size_t i = 0; i < num_hashes_; i++) {
        size_t idx = hash(&key, sizeof(K), i);
        if (!getBit(idx)) return false;
    }
    return true;
}
```

**Integration with Get:**

The Bloom filter check is integrated into the SST's get method. The `SSTBase` class provides a `contains` method that checks the bloom filter:

```cpp
bool contains(const K& key) const {
    if (!bloom_) return true;  // no filter, must search
    return bloom_->contains(key);
}
```

The B-tree SST's get method checks the bloom filter before searching.

```cpp
V* BTreeSST::get(const K& key) const override {
    // bloom filter check
    if (!this->contains(key)) return nullptr;

    return searchBTree(key);
}
```

It is similarly implemented for the binary (flat) SST.

#### Persisting Filters in SSTs

**Location:** `Part3/sst_base.hpp`, `Part3/btree_sst.hpp`, `Part3/bloom_filter.hpp`

Each SST has a companion Bloom filter file with extension `.bloom`. The base SST class manages filter creation and persistence.

**Building the Filter:**

The `SSTBase` class provides a helper to build the filter during SST creation:

```cpp
void buildBloomFilter(const std::vector<std::pair<K, V>>& entries, size_t bits_per_entry = 0) {
    if (bits_per_entry > 0) {
        bloom_ = std::make_unique<BloomFilter>(entries.size(), bits_per_entry);
    } else {
        bloom_ = std::make_unique<BloomFilter>(entries.size());
    }
    for (const auto& [k, v] : entries) {
        bloom_->add(k);
    }
}
```

**Writing the Filter:**

When writing an SST, we build and persist the Bloom filter:

```cpp
void BTreeSST::writeFromPairs(const std::vector<std::pair<K, V>>& entries) override {
    // build bloom filter for fast negative lookups
    this->buildBloomFilter(entries);

    writeBTree(entries);

    // write bloom filter to file
    std::ofstream bloom_file(filepath_ + ".bloom", std::ios::trunc | std::ios::binary);
    if (bloom_file.is_open()) {
        this->writeBloomToFile(bloom_file);
        bloom_file.close();
    }
}
```

**Filter File Format:**
```
[bloom_size: 4 bytes][num_bits: 8 bytes][num_hashes: 8 bytes][bit_array: variable]
```

The BloomFilter class provides writing and loading methods.

```cpp
void bloomWrite(uint8_t* out) const {
    memcpy(out, &num_bits_, sizeof(num_bits_));
    out += sizeof(num_bits_);
    memcpy(out, &num_hashes_, sizeof(num_hashes_));
    out += sizeof(num_hashes_);
    memcpy(out, bits_.data(), bits_.size());
}

static BloomFilter bloomLoad(const uint8_t* data) {
    size_t num_bits, num_hashes;
    memcpy(&num_bits, data, sizeof(num_bits));
    data += sizeof(num_bits);
    memcpy(&num_hashes, data, sizeof(num_hashes));
    data += sizeof(num_hashes);
    size_t byte_len = (num_bits + 7) / 8;
    return BloomFilter(data, byte_len, num_bits, num_hashes);
}
```

**Loading the Filter:**

When loading an SST, the base class constructor loads the bloom filter:

```cpp
SSTBase(const std::string& filepath, size_t maxSize, BufferPool* bp, bool useDirect = false)
    : bufferPool_(bp), filepath_(filepath), fd_(-1), maxSize_(maxSize),
      openFlags_(useDirect ? (O_RDONLY | O_DIRECT) : O_RDONLY)
{
    fd_ = open(filepath.c_str(), openFlags_);
    if (fd_ != -1) {
        loadBloomFromFile();
    }
}
```

#### Compaction/Merge of Two SSTs

**Location:** `Part3/kvstore.cpp`

We implement LSM-tree compaction with a fixed size ratio of 2 (adjustable) between adjacent levels.

**Level Structure:**

The KVStore maintains levels as a vector of SST vectors. By default there is up to 20 levels giving over 1 TB of storage. This can be adjusted.

```cpp
// levels_[0]=L1, levels_[1]=L2, ... 
std::vector<std::vector<SSTable<K, V>>> levels_;
size_t level_ratio_ = 2;  // ratio between LSM levels
static constexpr int MAX_DISK_LEVELS = 20;  // L1 through L20
```

- Level 0: Memtable (in-memory)
- Level 1 (levels_[0]): Up to 2 SSTs
- Level 2 (levels_[1]): Up to 4 SSTs
- Level 3 (levels_[2]): Up to 8 SSTs
- Level 4 (levels_[3]): Up to 16 SSTs
- ...

SST filenames encode their level: `<db_name>_L<level>_<sequence>`

**Compaction Trigger:**

After flushing the memtable, we check if any level exceeds capacity:

```cpp
size_t levelCapacity(int arr_idx) {
    return std::pow(level_ratio_, arr_idx + 1);
}

void maybeCompact() {
    for (int arr_idx = 0; arr_idx < MAX_DISK_LEVELS - 1; arr_idx++) {
        if (levels_[arr_idx].size() > levelCapacity(arr_idx)) {
            compact(arr_idx);
        }
    }
}
```

**Compaction Implementation:**

When compacting, we merge all SSTs at a level into a single SST at the next level using a min-heap:

```cpp
std::vector<std::pair<K, V>> mergeSort(std::vector<std::vector<std::pair<K, V>>>& inputs) {
    if (inputs.empty()) return {};
    
    // min-heap: (key, value, source_idx, pos_in_source)
    using Entry = std::tuple<K, V, size_t, size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

    // initialize heap with first element from each input
    for (size_t i = 0; i < inputs.size(); i++) {
        if (!inputs[i].empty()) {
            heap.push({inputs[i][0].first, inputs[i][0].second, i, 0});
        }
    }
    
    std::vector<std::pair<K, V>> result;
    K last_key{};
    bool has_last = false;
    
    while (!heap.empty()) {
        auto [key, value, src, pos] = heap.top();
        heap.pop();
        
        // skip duplicates, keeping first occurrence
        if (!has_last || key != last_key) {
            result.push_back({key, value});
            last_key = key;
            has_last = true;
        }
        
        // add next element from same source
        if (pos + 1 < inputs[src].size()) {
            heap.push({inputs[src][pos + 1].first, inputs[src][pos + 1].second, src, pos + 1});
        }
    }
    
    return result;
}
```

The compact method collects data from all SSTs at a level and merges them:

```cpp
void compact(int arr_idx) {
    if (arr_idx < 0 || arr_idx >= MAX_DISK_LEVELS - 1) return;
    if (levels_[arr_idx].empty()) return;

    // collect all pairs from this level
    std::vector<std::vector<std::pair<K, V>>> sst_data;
    for (auto& sst : levels_[arr_idx]) {
        auto pairs = sst.scan(std::numeric_limits<K>::min(),
                              std::numeric_limits<K>::max());
        sst_data.push_back(std::move(pairs));
    }

    // merge all SSTables
    std::vector<std::pair<K, V>> merged = mergeSort(sst_data);

    // drop tombstones when compacting to last disk level
    bool is_last_level = (arr_idx + 1 == MAX_DISK_LEVELS - 1);
    if (is_last_level) {
        std::vector<std::pair<K, V>> filtered;
        for (const auto& [k, v] : merged) {
            if (!isTombstone(v)) {
                filtered.push_back({k, v});
            }
        }
        merged = std::move(filtered);
    }

    // delete old SST files and their bloom filters
    for (size_t i = 0; i < levels_[arr_idx].size(); i++) {
        std::string filename = db_name_ + "/" + sstFilename(arr_idx, i);
        buffer_pool_.invalidateFile(filename);  // invalidate cached pages
        std::remove(filename.c_str());
        std::remove((filename + ".bloom").c_str());
    }
    levels_[arr_idx].clear();
    sst_indices_[arr_idx] = 0;

    // write merged data to next level
    // ...
}
```

The `invalidateFile` method is called during compaction before deleting SST files to prevent stale cached pages from being served when new files with the same names are created.

#### Support for Updates

**Location:** `Part3/kvstore.cpp`

Updates are handled by the LSM-tree structure. When a key is inserted multiple times, each insertion creates a new entry. The get operation searches from newest to oldest, returning the most recent value.

During compaction, when we encounter duplicate keys across SSTs being merged, we keep only the entry from the newer SST. The min-heap ordering (with SST age as a tiebreaker) ensures the first occurrence of each key is the newest. 

#### Support for Deletes

**Location:** `Part3/kvstore.cpp`

We implement deletes using tombstones, using the minimum numeric value as the marker:

```cpp
static constexpr V TOMBSTONE = std::numeric_limits<V>::min();

bool isTombstone(const V& value) const {
    return value == TOMBSTONE;
}

void del(const K& key) {
    put(key, TOMBSTONE);
}
```

**Get with Tombstones:**

When get finds a tombstone, it returns nullptr:

```cpp
V* get(const K& key) {
    // check L0 (memtable) first - most recent
    V* value = memtable_.get(key);
    if (value) {
        return isTombstone(*value) ? nullptr : value;
    }

    // search disk levels L1 to Ln (newest to oldest)
    for (size_t arr_idx = 0; arr_idx < levels_.size(); arr_idx++) {
        auto& level_ssts = levels_[arr_idx];
        
        for (auto it = level_ssts.rbegin(); it != level_ssts.rend(); ++it) {
            value = it->get(key);
            if (value) {
                return isTombstone(*value) ? nullptr : value;
            }
        }
    }

    return nullptr;
}
```

**Scan with Tombstones:**

Scan filters out tombstones from the result set:

```cpp
std::vector<std::pair<K, V>> scan(K start_key, K end_key) {
    std::map<K, V> merged;

    // scan disk levels oldest to newest so newer values overwrite
    for (int arr_idx = MAX_DISK_LEVELS - 1; arr_idx >= 0; arr_idx--) {
        // within each level, oldest to newest
        for (auto& sst : levels_[arr_idx]) {
            auto pairs = sst.scan(start_key, end_key);
            for (const auto& [k, v] : pairs) {
                merged[k] = v;
            }
        }
    }

    // L0 (memtable) is most recent
    auto mem_pairs = memtable_.scan(start_key, end_key);
    for (const auto& [k, v] : mem_pairs) {
        merged[k] = v;
    }

    // filter out tombstones from result
    std::vector<std::pair<K, V>> result;
    for (const auto& [k, v] : merged) {
        if (!isTombstone(v)) {
            result.push_back({k, v});
        }
    }
    return result;
}
```

**Tombstone Garbage Collection:**

Tombstones are only needed to mask older versions. Once a tombstone reaches the maximum level (Level 5), there are no older versions to mask, so we remove it during compaction.

---

## Project Status

### What Works

All core functionality required by the project specification has been implemented and tested:

**Step 1 - Memtable and SSTs:**
- AVL tree-based memtable with put, get, and scan operations
- SST creation when memtable reaches capacity
- Binary search over SSTs for point queries
- Range scans across memtable and multiple SSTs
- Database open/close with persistence

**Step 2 - Buffer Pool and B-Trees:**
- Hash table-based buffer pool with separate chaining for collision resolution
- Clock eviction policy for page replacement
- Static B-tree structure for SSTs with improved query performance
- Integration of buffer pool with all query paths
- Support for O_DIRECT I/O to bypass OS page cache

**Step 3 - LSM-Tree with Filters:**
- Bloom filter implementation with configurable bits per entry
- Filter persistence as companion files to SSTs
- Compaction with size ratio of 2 between levels
- K-way merge using min-heap for compaction
- Support for updates (overwriting existing keys)
- Support for deletes via tombstone markers

### Known Limitations

- The memtable size is measured in number of entries rather than bytes. A more accurate implementation would track actual memory usage.
- Compaction loads all data from a level into memory before merging. For very large levels, a streaming approach would be more memory-efficient.

### Known Bugs

None known at the time of submission. 

---
## Experiments

### Experiment 1: Binary Search vs B-tree Query Throughput

Done at the end of step 3.

**Setup:**
- Uniformly random point queries
- Buffer pool: 1MB (256 frames)
- 50K queries at each data size
- Data sizes: 50K to 400K entries

**Results:**

| Data Size | Binary Search (queries/sec) | B-tree (queries/sec) | Speedup |
|-----------|---------------------------|---------------------|---------|
| 50,000    | 291,897                   | 997,915             | 3.4x    |
| 100,000   | 271,453                   | 833,099             | 3.1x    |
| 150,000   | 35,034                    | 69,263              | 2.0x    |
| 200,000   | 11,431                    | 27,046              | 2.4x    |
| 250,000   | 6,945                     | 20,240              | 2.9x    |
| 300,000   | 5,586                     | 17,194              | 3.1x    |
| 350,000   | 4,872                     | 15,494              | 3.2x    |
| 400,000   | 4,251                     | 14,485              | 3.4x    |

![Binary Search vs B-tree Throughput](permanent_exp1.png)

**Findings:**

The results show a consistent advantage for B-tree indexing over binary search across all data sizes. At the smallest data size (50K entries), both approaches achieve high throughput since most data fits in the buffer pool. B-tree still wins here with almost 1 million queries per second compared to about 292K for binary search, a 3.4x improvement.

The most dramatic drop happens between 100K and 150K entries. This is when the working set exceeds the 1MB buffer pool capacity. Binary search throughput drops from 271K to 35K queries/sec (~87% drop), while B-tree drops from 833K to 69K (~92% drop). The drop is steeper for B-tree, but relative performance improves as data grows larger.

At 400K entries, B-tree maintains a 3.4x speedup over binary search. This makes sense when you think about the number of page accesses required. Binary search needs $O(\log_2 n)$ random page reads to locate a key—for 400K entries that's roughly 19 page accesses. B-tree with its high branching factor (around 240 keys per internal node in our implementation) needs only $O(\log_B n)$ accesses, which works out to about 2-3 page reads for the same data.

The other factor is cache locality. B-tree internal nodes pack many keys together, so each page read gives us more useful comparison data. Binary search jumps around the file essentially at random, leading to more cache misses.

One interesting observation: the speedup ratio dips to 2.0x right at 150K entries, then climbs back up.

### Experiment 2: Throughput Over 1GB Insertion

**Setup:**
- Buffer pool: 10MB
- Bloom filter: 8 bits per entry
- Memtable: 1MB
- Total data: 1GB
- Measurements every 100MB

**Results:**

| Data Size (MB) | Insert Throughput (ops/sec) | Get Throughput (ops/sec) | Scan Throughput (keys/sec) |
|----------------|----------------------------|--------------------------|---------------------------|
| 100            | 203,869                    | 1,942                    | 597,602                   |
| 200            | 121,462                    | 1,489                    | 425,733                   |
| 300            | 120,779                    | 1,400                    | 445,427                   |
| 400            | 204,660                    | 1,206                    | 150,532                   |
| 500            | 121,462                    | 1,188                    | 159,364                   |
| 600            | 121,570                    | 1,175                    | 170,190                   |
| 700            | 127,224                    | 1,116                    | 147,106                   |
| 800            | 193,175                    | 1,052                    | 86,286                    |
| 900            | 121,281                    | 1,048                    | 98,483                    |
| 1000           | 126,237                    | 1,007                    | 90,886                    |

![Throughput Over 1GB Insertion](permanent_exp2.png)

**Findings:**

**Insert Throughput:** The insert performance shows a oscillating pattern, bouncing between ~120K and ~200K ops/sec. The peaks at 100MB, 400MB, and 800MB likely correspond to moments right after a major compaction completes. When compaction finishes, the LSM tree has fewer levels to manage temporarily, and writes go to the memtable. The dips happen when the system is either mid-compaction or when multiple levels are filling up simultaneously. Overall though, insert throughput stays relatively stable.

**Get Throughput:** Point query performance shows a steady decline from ~1,942 ops/sec at 100MB down to ~1,007 ops/sec at 1GB (~48% drop). This degradation is expected in LSM trees. As data accumulates, queries potentially need to check more levels before finding the target key. At 1GB with a 1MB memtable and size ratio of 2, we're looking at around 5 populated levels. The bloom filter helps skip SSTables that definitely don't contain the key, but when a key does exist, we still need to traverse down through the levels. The fact that throughput only halved over a 10x increase in data size shows the bloom filter is doing its job.

**Scan Throughput:** Range scans take the biggest hit, dropping from ~598K keys/sec at 100MB to ~91K keys/sec at 1GB (~85% reduction). This makes sense because scans can't benefit from bloom filters as they would need to look at all potentially overlapping ranges. At 1GB, a scan has to merge data from multiple SSTables across several levels, and each level adds overhead to the merge iterator. The sharp drop between 300MB and 400MB (from 445K to 150K) is particularly notable. This is probably when we hit a threshold where scans start spanning significantly more SSTables.

---
## Testing

### Unit Tests

**Location:** `Part1/test.cpp`, `Part2/unit_test.cpp`, `Part3/unit_test.cpp`

We implemented unit tests for each part to verify correctness of all operations. The tests use assertions and print pass/fail status. Part2 and Part3 unit tests are adapted from Part1 with the additional buffer pool parameter.

**Core Test Cases (all parts):**

1. **testBasicOperations** - Verifies basic put and get operations
2. **testMemtableFlush** - Verifies memtable correctly flushes to SST when full
3. **testUpdateValue** - Verifies that updating a key returns the new value
4. **testScan** - Verifies range scan functionality with inclusive bounds
5. **testScanAcrossSSTables** - Verifies that scan correctly merges results from memtable and multiple SSTs
6. **testMultipleSSTables** - Verifies lookups across multiple SST files created by repeated flushes and compaction

**Additional Part3 Test Cases:**

7. **testDelete** - Verifies that deleting a key makes it return nullptr while other keys remain accessible
8. **testDeleteInScan** - Verifies that deleted keys are excluded from scan results

**Running Unit Tests:**

```bash
# Part1
cd Part1
make test
./test

# Part2
cd Part2
make unit_test
./unit_test

# Part3
cd Part3
make unit_test
./unit_test
```

Sample output (Part3):
```
=== basic put and get ===
PASSED

=== memtable flush to sstable ===
PASSED

=== update existing key ===
PASSED

=== scan range ===
PASSED

=== scan across multiple sstables ===
PASSED

=== multiple sstable lookups ===
PASSED

=== delete key ===
PASSED

=== deleted keys excluded from scan ===
PASSED

=== all tests passed ===
```

### Performance Tests

**Location:** `Part2/perf_test.cpp`, `Part3/perf_test.cpp`

The Part2 and Part3 performance test files implement benchmarks comparing binary search SSTs against B-tree SSTs:

```cpp
static constexpr int N = 200000;  // number of entries in SST
static constexpr int Q = 50000;   // number of random queries
static constexpr int FRAMES = 256; // buffer pool size

double runTest(SSTSearchMode mode) {
    auto data = makeData();
    BufferPool pool(FRAMES);

    SSTable<int,int> sst(file, N, &pool, mode);
    sst.writeFromPairs(data);

    auto queries = makeQueries();
    pool.resetStats();

    auto t0 = Clock::now();
    for (int key : queries) {
        int* v = sst.get(key);
        if (v && *v == key * 10) correct++;
        delete v;
    }
    auto t1 = Clock::now();
    
    // output timing and buffer pool statistics
    // ...
}
```

Sample output:
```
=== SST Binary vs B-Tree Comparison ===
[BINARY]
Correct = 50000/50000
Time    = 2.34 sec
Hits    = 156789
Misses  = 43211
Evict   = 42955

[B-TREE]
Correct = 50000/50000
Time    = 0.87 sec
Hits    = 198234
Misses  = 1766
Evict   = 1510

Speedup (Binary / B-tree): 2.69x
```

**Running Performance Tests:**

```bash
# Part2
cd Part2
make perf_test
./perf_test

# Part3
cd Part3
make perf_test
./perf_test
```

---

## Compilation and Running Instructions

### Prerequisites

- Linux operating system
- g++ compiler with C++17 support
- Make build system

### Directory Structure

```
CSC443-Project/
    README.md
    REPORT.md          # This report
    Part1/             # Step 1 implementation
    Part2/             # Step 2 implementation  
    Part3/             # Step 3 implementation (final)
        experiments/   # Experiment code and notebooks
```

### Building

To build the test executables:

```bash
cd Part3
make unit_test
make perf_test
```

To build the experiment executable:

```bash
cd Part3
make experiments/experiment
```

To clean build artifacts:

```bash
cd Part3
make clean
```

Similar for part 1 and 2. Part 1 only has `test` make target.

### Running Tests

```bash
cd Part3
./unit_test
./perf_test
```

Similar for part 2. Part 1 only has `./test`.

### Running Experiments

Build the experiment executable first, then run with argument 1 or 2:

Experiment 1 (Binary vs B-tree throughput):
```bash
cd Part3
make experiments/experiment
./experiments/experiment 1
```

Experiment 2 (Throughput over 1GB insertion):
```bash
cd Part3
./experiments/experiment 2
```

Alternatively, all of this is handled in `Part3/experiment/experiment.ipynb`.

---

