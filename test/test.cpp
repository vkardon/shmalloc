//
// test.cpp
//
#include "shmalloc.hpp"
#include "utils.hpp"
#include <list>
#include <random>

static bool g_InfoEnabled = false; // Enable/Disable INFO() logging
#define INFO(msg)  if(g_InfoEnabled) shmLOG(std::cout, "INFO", msg)
#define ERROR(msg)                   shmERROR(msg)

class Data
{
public:
    Data() = default;
    ~Data() = default;
    Data* next{0};
};

//
// Actual test routine
//
bool TestAllocatorImpl(std::unique_ptr<mem::ShmAlloc>& al, int childrenCount,
                       const std::vector<int>& allocSizes)
{
    size_t totalAllocations = 0;

    //childrenCount = 1; // victor test
    Data** dataArr = new (al) Data*[childrenCount]{};
    totalAllocations++;

    INFO("Parent: Spawning " << childrenCount << " child processes");

    auto childProc = [&](int childIndex) 
    {
        // Running as a child
        // Allocate a memory chunk for every allocation and construct the Data object
        // in that location using placement new. Then, link the current Data object
        // to the previous one, forming the chain (which can be traversed later
        // to validate the allocations).
        Data* prev = nullptr;
        for(int size : allocSizes)
        {
            void* ptr = al->alloc(size);    // Allocate a memory chunk
            Data* data = new (ptr) Data();  // Construct the Data object

            // Link the current Data object to the previous one
            prev ? prev->next = data : dataArr[childIndex] = data;
            prev = data;
        }

        INFO("Child " << childIndex << ": Done, made " << allocSizes.size() << " allocations");
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
    for(int childIndex = 0; childIndex < childrenCount; childIndex++)
    {
        int chilAllocations = 0;
        for(Data* data = dataArr[childIndex]; data; data = data->next)
        {
            chilAllocations++;
        }
        INFO("Parent: Child " << childIndex << " allocations count " << chilAllocations);
        totalAllocations += chilAllocations;
    }

    INFO("Parent: Total allocations count " << totalAllocations);

    // // Parent can keep allocation after memory striping is off
    // void* ptr = new (al) char[1024 * 1024 * 1024]; // 1GB
    // std::cout << "ptr=" << ptr << std::endl;

    return true;
}

void TestAllocator(int childrenCount, int allocCount)
{
    std::string separtor("----------------------------------------------------");
    std::cout << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << " Number of processes : " << childrenCount << std::endl;
    std::cout << " Objects per process : " << allocCount << std::endl;
    std::cout << " Total objects       : " << allocCount * childrenCount << std::endl;
    std::cout << separtor << std::endl;

    bool useFixedPositionAllocator = false;
    std::unique_ptr<mem::ShmAlloc> alloc;

    if(useFixedPositionAllocator)
    {
        // Create a fixed-position allocator using an address range.
        // // Note: Start/stop positions are system-dependent; use them with caution.
        void* start = reinterpret_cast<void*>(mem::ALLOC_MIN_ADDR + 5UL * 1024 * 1024 * 1024);    // 5GB offset from mem::ALLOC_MIN_ADDR
        void* stop = reinterpret_cast<void*>(mem::ALLOC_MIN_ADDR + 256UL * 1024 * 1024 * 1024);   // 256GB allocator size
        alloc.reset(mem::ShmAlloc::Create("MyAllocator", start, stop));
        if(!alloc)
        {
            ERROR("Failed to create allocator: start=" << start << ", stop=" << stop);
            return;
        }
    }
    else
    {
        // Create a position-independent allocator that uses a specified size.
        std::size_t size = 256UL * 1024 * 1024 * 1024; // 256GB allocator size
        alloc.reset(mem::ShmAlloc::Create("MyAllocator", size));
        if(!alloc)
        {
            ERROR("Failed to create allocator: size=" << size);
            return;
        }
    }
    // alloc->enableInfoLog(true);
    // alloc->enableDebugLog(true);
 
    // Create a vector of integers to store a list of randomly
    // determined sizes within a specific min-max range
    std::vector<int> allocSizes(allocCount);
    std::random_device rd;
    std::mt19937 generator(rd());
    // std::uniform_int_distribution<> distrib(2050, 4096 /*max*/); //<-- failed if max greater rhen 2048 TODO
    // std::uniform_int_distribution<> distrib(sizeof(Data) /*min*/, 4096 /*max*/); //<-- failed if max greater rhen 2048 TODO
    // std::uniform_int_distribution<> distrib(sizeof(Data) /*min*/, 2050 /*max*/); //<-- failed if max greater rhen 2048 TODO
    std::uniform_int_distribution<> distrib(sizeof(Data) /*min*/, 128 /*max*/);
    for(auto& s : allocSizes)
        s = distrib(generator);

    // std::vector<int> allocSizesTmp
    // {
    //     32,8,886,1874,2055,993,4045,2877,441,1346,1225,
    //     1789,819,864,850,3158,1632,2680,3886,2044,177,3075,
    //     1020,4071,682,3320,2960,2123,1290,2432,4069,2190,500,
    //     900,1232,1592,3061,2306,358,2136,393,3713,272,1553,1412,
    //     2661,912,3703,2249,3062,2501,781,2566,1645,3908,10,1185,
    //     3174,1617,3985,1870,773,2383,3665,1924,147,452,2890,2297,
    //     3510,1279,990,4064,536,2262,2638,422,2436,2480,1206,1377,
    //     80,3432,3637,2688,2670,601,205,3375,1265,2938,2574,1888,
    //     282,640,978,2324,24,2245,591,1866,2091,3944,826,2880,732,
    //     2180,1655,2150,14,278,3281,3895,3603,2422,2033,1126,1917,
    //     3844,3103,3735,851,1030,1854,2982,2471,3248,2446,2759,2216,
    //     284,3605,782,1093,2777,1013,1771,1001,587,3886,2722,1824,
    //     2416,2130,3686,2477,3048,3876,1660,1009,123,1734,1992,3455,
    //     2179,1650,1043,1934,106,2025
    // };

    // allocSizes.insert(allocSizes.end(),
    //     std::make_move_iterator(allocSizesTmp.begin()), std::make_move_iterator(allocSizesTmp.end()));

    // Run the test
    {
        StopWatch sw;
        if(!TestAllocatorImpl(alloc, childrenCount, allocSizes))
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

void TestAllocatorSlots()
{
    // TODO: Create() to return nullptr when size is less then 16 MB 
    // as start / stop addresses must be 16MB aligned.

    std::string separtor("----------------------------------------------------");
    std::cout << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << separtor << std::endl;

    StopWatch sw;

    // Create a position-independent allocator that uses a specified size.
    // Note: Allocator size is not relevant for this test.
    constexpr std::size_t size = 1024 * 1024 * 16; // 16MB
    std::unique_ptr<mem::ShmAlloc> alloc(mem::ShmAlloc::Create("MyAllocator", size));
    if(!alloc)
    {
        std::cerr << "Failed to create allocator: size=" << size << std::endl;
        return;
    }

    // Run the test
    alloc->testFindSlot();
}

