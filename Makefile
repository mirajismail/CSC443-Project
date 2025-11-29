# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -g

# Source files (explicitly list them where they currently are)
# Source files
SRCS = \
    Part1/kvstore.cpp \
    Part1/memtable.cpp \
    Part1/sstable.cpp \
    Part1/avltree.cpp \
    Part2/buffer_pool.cpp \
    Part1/test.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Executable name
TEST_EXE = test

# Default target
all: $(TEST_EXE)

# Build test executable
$(TEST_EXE): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TEST_EXE) $(OBJS)

# Compile source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f Part\ 1/*.o Part\ 2/*.o $(TEST_EXE) teststore_*

.PHONY: all clean
