# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -g

# Source files
SRCS = kvstore.cpp memtable.cpp sstable.cpp avltree.cpp test.cpp

# Object files
OBJS = kvstore.o memtable.o sstable.o avltree.o test.o

# Executable names
TEST_EXE = test

# Default target: build both executables
all: $(TEST_EXE)

# Build test executable
$(TEST_EXE): test.o memtable.o sstable.o avltree.o kvstore.o
	$(CXX) $(CXXFLAGS) -o $(TEST_EXE) test.o memtable.o sstable.o avltree.o kvstore.o

# Compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f *.o $(TEST_EXE) teststore_*

.PHONY: all clean
