//
// test.cpp
//
#include "shmalloc.hpp"
#include "utils.hpp"
#include <list>

static bool g_InfoEnabled = false; // Enable/Disable INFO() logging
#define INFO(msg)  if(g_InfoEnabled) shmLOG(std::cout, "INFO", msg)
#define ERROR(msg)                   shmERROR(msg)

class Data
{
public:
    Data(unsigned int id) : mId(id) {}
    ~Data() = default;

    unsigned int GetId() const { return mId; }

private:
    unsigned int mId = 0;

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
bool TestStlAllocatorImpl(std::unique_ptr<mem::ShmAlloc>& al, int childrenCount, int allocCount)
{
    //childrenCount = 1; // victor test
    TestObject** testObjects = new (al) TestObject*[childrenCount]{};

    INFO("Parent: Spawning " << childrenCount << " child processes");

    auto childProc = [&](int childIndex) 
    {
        // Running as a child
        TestObject* testObj = new (al) TestObject;

        for(int i = 0; i < allocCount; i++)
            testObj->dataList.emplace_back(i);

        testObjects[childIndex] = testObj;

        INFO("Child " << childIndex << ": Done, testObj " << testObj << 
             ", created " << testObj->dataList.size() << " objects");
    };

    // Fork children...
    if(!ForkAndWait(childrenCount, childProc))
    {
        ERROR("Failed to fork or some of the children processes returned error");
        return false;
    }

    // Running as a parent.
    INFO("Parent: All children completed");

    // Access children't data
    int totalObjects = 0;
    for(int childIndex = 0; childIndex < childrenCount; childIndex++)
    {
        TestObject* testObj = testObjects[childIndex];
        if(!testObj)
        {
            ERROR("Child " << childIndex << " testObj is NULL");
            continue;
        }

        INFO("Child " << childIndex << ": testObj " << testObj << 
             ", got " << testObj->dataList.size() << " objects");
        totalObjects += testObj->dataList.size();
    }

    if(totalObjects != childrenCount * allocCount)
    {
        ERROR("Expected " << (childrenCount * allocCount) << ", received " << totalObjects << " objects");
        return false;
    }

    // // Parent can keep allocation after memory striping is off
    // void* ptr = new (al) char[1024 * 1024 * 1024]; // 1GB
    // std::cout << "ptr=" << ptr << std::endl;

    return true;
}

void TestStlAllocator(int childrenCount, int allocCount)
{
    std::string separtor("----------------------------------------------------");
    std::cout << separtor << std::endl;

    std::size_t objTotal = allocCount * childrenCount;
    std::size_t objSize = sizeof(Data);
    std::size_t perProcessSize = sizeof(Data) * allocCount;
    std::size_t totalSize = sizeof(Data) * objTotal;

    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << " Number of processes : " << childrenCount << std::endl;
    std::cout << " Objects per process : " << allocCount << std::endl;
    std::cout << " Total objects       : " << objTotal << std::endl;
    std::cout << " Object size         : " << BytesToStr(objSize) << std::endl;
    std::cout << " Size per process    : " << BytesToStr(perProcessSize) << std::endl;
    std::cout << " Total size          : " << BytesToStr(totalSize) << std::endl;
    std::cout << separtor << std::endl;

    // Create a position-independent allocator that uses a specified size.
    std::size_t size = 256UL * 1024 * 1024 * 1024; // 256GB allocator size
    std::unique_ptr<mem::ShmAlloc> alloc(mem::ShmAlloc::Create("MyAllocator", size));
    if(!alloc)
    {
        ERROR("Failed to create allocator: size=" << size);
        return;
    }

    // victor test
    // FTRACE("Allocating TestObject");
    // new (alloc) char[25];
    // new (alloc) char[8];
    // // new (alloc) char[3051];
    // // alloc->alloc(1024);
    // // alloc->alloc(2048);
    // // alloc->alloc(0);
    // // new (alloc) char[25];
    // // new (alloc) char[25];

    // [[maybe_unused]] TestObject* obj = new (alloc) TestObject;
    // delete obj;
    // obj = new (alloc) TestObject;

    /*
    char* ptr = new (alloc) char[8195];
    delete ptr;

    std::vector<char*> arr;
    for(int i=0; i < 376; i++)
    {
        auto p = new (alloc) char [2048];
        arr.push_back(p);
    }
    */

    // std::cout << "arr.size=" << arr.size() << std::endl;
    // for(char* p : arr)
    //     delete[] p;

    // std::cout << std::endl;
    // return;
    // victor test

    // Run the test
    {
        StopWatch sw;
        if(!TestStlAllocatorImpl(alloc, childrenCount, allocCount))
            return;
    }

    // Report memory allocator statistics
    size_t virtualSize = alloc->getVSZ();
    size_t residentSize = alloc->getRSS();

    std::size_t totalRequested = 0;
    std::size_t totalAllocated = 0;
    std::size_t totalAllocCount = 0; 
    alloc->getTotals(totalRequested, totalAllocated, totalAllocCount);

    std::cout << "  Virtual Size (VSZ) : " << BytesToStr(virtualSize)    << " (" << virtualSize  << ")" << std::endl;
    std::cout << " Resident Size (RSS) : " << BytesToStr(residentSize)   << " (" << residentSize << ")" << std::endl;
    std::cout << "    Requested Memory : " << BytesToStr(totalRequested) << " (" << totalRequested << ")" << std::endl;
    std::cout << "    Allocated Memory : " << BytesToStr(totalAllocated) << " (" << totalAllocated << ")" << std::endl;
    std::cout << "         Alloc Count : " << totalAllocCount << " allocations" << std::endl;

    // std::cout << "  Used: " << BytesToStr(alloc->getUsed()) << std::endl;
    // std::cout << "  Free: " << BytesToStr(alloc->getFree()) << std::endl;

    // alloc->audit(stdout, std::string("From ") + __func__);
}


