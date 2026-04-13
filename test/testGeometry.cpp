//
// testStress.cpp
//
#include <iostream>
#include <vector>
#include <random>
#include "shmalloc.hpp"
#include "utils.hpp"

// Verify that every pointer returned by alloc can be correctly
// mapped back to the ShmAlloc instance using your AllocTable.
void TestPointerDiscovery(mem::ShmAlloc* allocator)
{
    std::string separtor("----------------------------------------------------");
    std::cout << '\n' << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << separtor << std::endl;

    void* ptr = allocator->alloc(1024);
    if (ptr != nullptr)
    {
        // This tests the GET_ALLOC_BUCKET macro and AllocTable logic
        mem::Alloc* found = mem::GetAllocator(ptr);
        if (found != allocator)
        {
            std::cerr << "Discovery test FAILED: Global AllocTable lookup failed!" << std::endl;
        }
        else
        {
            std::cout << "Discovery test PASSED." << std::endl;
        }
        allocator->free(ptr);
    }
}

// The Geometry & Bounds Test:
// This test checks if the allocator correctly identifies its own boundaries 
// and handles the maximum possible allocation allowed by your size-class logic
void TestGeometryAndBounds(mem::ShmAlloc* allocator)
{
    std::string separtor("----------------------------------------------------");
    std::cout << '\n' << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << separtor << std::endl;

    // Test 1: Smallest possible allocation
    void* pSmall = allocator->alloc(1);
    if (pSmall == nullptr)
    {
        std::cerr << "Geometry test FAILED: Failed smallest allocation (1 byte)" << std::endl;
    }
    else
    {
        if (reinterpret_cast<uintptr_t>(pSmall) % 8 != 0)
        {
            std::cerr << "Geometry test FAILED: Small allocation not 8-byte aligned" << std::endl;
        }
        allocator->free(pSmall);
    }

    // Test 2: Large allocation (crossing the 2KB MAXSMALL boundary)
    size_t largeSize = 4096;
    void* pLarge = allocator->alloc(largeSize);
    if (pLarge == nullptr)
    {
        std::cerr << "Geometry test FAILED: Failed large allocation (4KB)" << std::endl;
    }
    else
    {
        // Verify the allocator recognizes this pointer
        if (mem::GetAllocator(pLarge) != allocator)
        {
            std::cerr << "Geometry test FAILED: GetAllocator failed to identify pointer owner" << std::endl;
        }
        allocator->free(pLarge);
    }

    std::cout << "Geometry Test PASSED" << std::endl;
}

// The Atomic "Race" Test: 
// This test uses ForkAndWait to specifically hammer the getMemory path.
void TestConcurrentExpansion(mem::ShmAlloc* allocator, int numProcs)
{
    std::string separtor("----------------------------------------------------");
    std::cout << '\n' << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << separtor << std::endl;

    std::cout << "Starting Expansion Test (" << numProcs << " procs) ..." << std::endl;

    bool result = ForkAndWait(numProcs, [allocator](int index)
    {
        // Each child tries to grab a unique 1MB chunk to force getMemory calls
        size_t bigSize = 1024 * 1024; 
        void* p = allocator->alloc(bigSize);

        if (p == nullptr)
        {
            std::cerr << "Child " << index << " failed to expand memory!" << std::endl;
            _exit(1); 
        }

        // Keep it allocated for a moment to ensure others have to "bump" further
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        allocator->free(p);
    });

    if (result)
    {
        std::cout << "Expansion test PASSED." << std::endl;
    }
    else
    {
        std::cerr << "Expansion test FAILED: Likely a race condition in SharedData::top" << std::endl;
    }
}

void TestMemoryRecycling(mem::ShmAlloc* allocator)
{
    std::string separtor("----------------------------------------------------");
    std::cout << '\n' << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << separtor << std::endl;

    std::cout << "Starting Recycling Test ..." << std::endl;
    
    size_t testSize = 1024 * 512; // 512 KB
    size_t initialUsed = allocator->getUsed();

    std::cout << "Initial used: " << BytesToStr(initialUsed) << std::endl;

    // 1. Allocate and Free the same block multiple times
    for (int i = 0; i < 10; ++i)
    {
        void* p = allocator->alloc(testSize);
        if (p == nullptr)
        {
            std::cerr << "Recycling test FAILED: Recycling failed at iteration " << i << std::endl;
            return;
        }
        allocator->free(p);
    }

    size_t finalUsed = allocator->getUsed();
    std::cout << "Final used after 10 cycles: " << BytesToStr(finalUsed) << std::endl;

    // 2. Verification: The 'used' count should not have grown 10x
    // It should stay roughly the same if recycling works.
    if (finalUsed > (initialUsed + (testSize * 2)))
    {
        std::cerr << "Recycling test WARNING: Memory does not seem to be recycling efficiently!" << std::endl;
    }
    else
    {
        std::cout << "Recycling test PASSED." << std::endl;
    }
}

void TestGeometry()
{
    mem::ShmAlloc* myAlloc = mem::ShmAlloc::Create("DebugAlloc", 1024 * 1024 * 128, true);

    if (myAlloc != nullptr)
    {
        TestPointerDiscovery(myAlloc);

        TestGeometryAndBounds(myAlloc);
        
        // Start with 2 processes to see if they collide, then scale up
        TestConcurrentExpansion(myAlloc, 12);

        TestMemoryRecycling(myAlloc);
        
        delete myAlloc;
    }
}
