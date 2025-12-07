# Compiler and flags
CXX = g++
CC = gcc
CXXFLAGS = -Wall -Wextra -std=c++17 -g
CFLAGS = -Wall -Wextra -g

# Source files
SRCS = kvstore.cpp memtable.cpp sstable.cpp avltree.cpp buffer_pool.cpp

# Executable names
UNIT_TEST_EXE = unit_test
PERF_TEST_EXE = perf_test
EXP_EXE = experiments/experiment

# Default target: build unit tests
all: $(UNIT_TEST_EXE)

# Build unit test executable
$(UNIT_TEST_EXE): unit_test.o memtable.o sstable.o avltree.o kvstore.o buffer_pool.o xxhash.o
	$(CXX) $(CXXFLAGS) -o $(UNIT_TEST_EXE) unit_test.o memtable.o sstable.o avltree.o kvstore.o buffer_pool.o xxhash.o

# Build perf test executable
$(PERF_TEST_EXE): perf_test.o memtable.o sstable.o avltree.o kvstore.o buffer_pool.o xxhash.o
	$(CXX) $(CXXFLAGS) -o $(PERF_TEST_EXE) perf_test.o memtable.o sstable.o avltree.o kvstore.o buffer_pool.o xxhash.o

# Build experiment executable
$(EXP_EXE): experiments/experiment.o buffer_pool.o xxhash.o
	$(CXX) $(CXXFLAGS) -o $(EXP_EXE) experiments/experiment.o buffer_pool.o xxhash.o

experiments/experiment.o: experiments/experiment.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

xxhash.o: xxhash.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f *.o $(UNIT_TEST_EXE) $(PERF_TEST_EXE) $(EXP_EXE) experiments/*.o
	rm -f teststore_* exp1_* exp2_* experiments/exp1_* experiments/exp2_*
	rm -f sst_binary sst_btree *.bloom experiment

.PHONY: all clean
