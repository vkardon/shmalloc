//
// testStress.cpp
//
#include <iostream>
#include <vector>
#include <random>
#include "shmalloc.hpp"
#include "utils.hpp"

// A simple structure to store inside our allocated memory to verify integrity
// Note: Ensure TestData is aligned to 8 bytes to avoid alignment faults on some architectures
struct alignas(8) TestData
{
    uint32_t processId;
    uint32_t iteration;
    uint64_t magic;
};

void RunAllocatorStress(mem::ShmAlloc* allocator, int processIndex)
{
    // unique_seed: PID + index to ensure different processes don't follow the same pattern
    unsigned int seed = static_cast<unsigned int>(getpid() ^ (processIndex << 16));
    std::mt19937 generator(seed);

    // Minimum size must be sizeof(TestData) to avoid buffer overflows
    // Maximum size 1MB to keep the test within the 256GB limit comfortably
    std::uniform_int_distribution<size_t> sizeDist(sizeof(TestData), 1024 * 1024);

    const int iterations = 5000;
    std::vector<void*> activeAllocations;
    activeAllocations.reserve(iterations);

    for(int i = 0; i < iterations; ++i)
    {
        size_t requestSize = sizeDist(generator);
        
        // Stage 1: Allocation
        void* ptr = allocator->alloc(requestSize);

        if(ptr != nullptr)
        {
            // Verify alignment
            if(reinterpret_cast<uintptr_t>(ptr) % 8 != 0)
            {
                std::cerr << "Process " << processIndex 
                          << " [ERR]: Pointer " << ptr << " is not 8-byte aligned!" << std::endl;
                _exit(1);
            }

            // Write verification data
            TestData* data = static_cast<TestData*>(ptr);
            data->processId = static_cast<uint32_t>(processIndex);
            data->iteration = static_cast<uint32_t>(i);
            data->magic = 0xABCDEFFF12345678ULL;

            activeAllocations.push_back(ptr);
        }
        else
        {
            // If allocator returns null, we might be out of memory or fragmented
            std::cerr << "Process " << processIndex 
                      << " [ERR]: alloc(" << requestSize << ") returned NULL" << std::endl;
            _exit(1);
        }

        // Stage 2: Fragmentation & Yielding
        // Every few iterations, shuffle and free a random portion
        if(i % 5 == 0 && !activeAllocations.empty())
        {
            // std::shuffle creates much more complex fragmentation than linear freeing
            std::shuffle(activeAllocations.begin(), activeAllocations.end(), generator);
            
            void* toFree = activeAllocations.back();
            TestData* data = static_cast<TestData*>(toFree);

            // Verify integrity BEFORE freeing
            if(data->magic != 0xABCDEFFF12345678ULL || data->processId != (uint32_t)processIndex)
            {
                std::cerr << "Process " << processIndex << " [ERR]: Memory corruption! "
                          << "Expected PID " << processIndex << ", found " << data->processId << std::endl;
                _exit(1);
            }

            allocator->free(toFree);
            activeAllocations.pop_back();
        }

        // Stage 3: Force Contention
        // Yielding forces the OS to context switch, often while a lock might be contested
        if(i % 10 == 0)
        {
            std::this_thread::yield();
        }
    }

    // Final cleanup
    for(void* ptr : activeAllocations)
    {
        allocator->free(ptr);
    }
}

void TestAllocatorStress()
{
    std::string separtor("----------------------------------------------------");
    std::cout << '\n' << separtor << std::endl;
    std::cout << " ### Test name       : " << __func__ << std::endl;
    std::cout << separtor << std::endl;

    const size_t shmSize = 1024 * 1024 * 1024 * 256UL; // 256 GB
    const int numProcesses = 12;

    std::cout << "Initializing ShmAlloc: " << BytesToStr(shmSize) << "..." << std::endl;
    
    // Create the allocator (using the size-based overload)
    mem::ShmAlloc* myAlloc = mem::ShmAlloc::Create("StressTestAlloc", shmSize, false);

    if(myAlloc == nullptr)
    {
        std::cerr << "Failed to create allocator." << std::endl;
        return;
    }

    std::cout << "Starting stress test with " << numProcesses << " processes..." << std::endl;

    bool success = false;
    {
        StopWatch timer("Stress test took: ");
        success = ForkAndWait(numProcesses, [myAlloc](int index)
        {
            RunAllocatorStress(myAlloc, index);
        });
    }

    if(success)
    {
        std::cout << "Stress test PASSED." << std::endl;
        //myAlloc->audit(); // Print final statistics
    }
    else
    {
        std::cerr << "Stress test FAILED: One or more processes encountered an error." << std::endl;
        myAlloc->audit(); // Print final statistics
    }

    // Destructor will handle munmap
    delete myAlloc;
}