//
// main.cpp
//
#include <iostream>

// Forward declarations
void TestAllocator(int childrenCount, int allocCount);
void TestStlAllocator(int childrenCount, int allocCount);

int main()
{
    int childrenCount = 6;
    //int childrenCount = 16;
    //int childrenCount = 50;
    //std::size_t allocCount = 100000000;
    //std::size_t allocCount = 30000000;
    //std::size_t allocCount = 10000000;
    std::size_t allocCount = 3000000;
    //std::size_t allocCount = 500000;
    //std::size_t allocCount = 50000;
    //std::size_t allocCount = 50;
 
    TestAllocator(childrenCount, allocCount);
    TestStlAllocator(childrenCount, allocCount);
    return 0;
}

