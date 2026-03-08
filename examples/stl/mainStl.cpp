//
// main.cpp
//
#include <iostream>
#include "shmalloc.hpp"
#include "utils.hpp"
#include <list>

class Data
{
public:
    Data(int _id) { id = _id; }
    ~Data() = default;

private:
    int id{-1};

    char data[16]{};
//    char data[32]{};
//    char data[64]{};
//    char data[128]{};
//    char data[265]{};
//    char data[512]{};
//    char data[1024]{};        // 1KB
//    char data[1024*1024]{};   // 1MB
};

using DataAllocator = mem::StlAlloc<Data>;

class TestObject
{
public:
    // Note: dataList will use TestObject's allocator for its elements
    TestObject() : dataList(DataAllocator(mem::GetAllocator(this))) {}
    ~TestObject() = default;

    std::list<Data, DataAllocator> dataList;
};

//
// Actual test routine
//
bool TestStlAllocator(int childrenCount, int allocCount)
{
    // Create a position-independent allocator that uses a specified size.
    constexpr std::size_t size = 1024 * 1024 * 1024; // 1GB
    std::unique_ptr<mem::ShmAlloc> al(mem::ShmAlloc::Create("MyAllocator", size));
    if(!al)
    {
        std::cerr << "Failed to create allocator: size=" << size << std::endl;
        return false;
    }

    // Array to collect results from child process execution 
    TestObject** testObjects = new (al) TestObject*[childrenCount]{};

    std::cout << "Parent: Spawning " << childrenCount << " child processes" << std::endl;

    auto childProc = [&](int childIndex) 
    {
        // Running as a child
        TestObject* testObj = new (al) TestObject;

        for(int i = 0; i < allocCount; i++)
            testObj->dataList.emplace_back(i);

        testObjects[childIndex] = testObj;
    };

    // Fork children...
    if(!ForkAndWait(childrenCount, childProc))
    {
        std::cerr << "Failed to fork or some of the children processes returned error" << std::endl;
        return false;
    }

    // Running as a parent.
    std::cout << "Parent: All children completed" << std::endl;

    // Access children't data
    int totalObjects = 0;
    for(int childIndex = 0; childIndex < childrenCount; childIndex++)
    {
        TestObject* testObj = testObjects[childIndex];
        if(!testObj)
        {
            std::cerr << "Child " << childIndex << " testObj is NULL" << std::endl;
            continue;
        }
        totalObjects += testObj->dataList.size();

        std::cout << "Child " << childIndex << ": testObj " << testObj << 
                     ", got " << testObj->dataList.size() << " objects" << std::endl;
    }

    if(totalObjects != childrenCount * allocCount)
    {
        std::cerr << "Expected " << (childrenCount * allocCount) << ", received " << totalObjects << " objects" << std::endl;
        return false;
    }

    return true;
}

int main()
{
    int childrenCount = 4;
    std::size_t allocCount = 50000;
 
    TestStlAllocator(childrenCount, allocCount);
    return 0;
}

