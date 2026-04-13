//
// testStress.cpp
//
#include <iostream>
#include <vector>
#include <random>
#include "shmalloc.hpp"
#include "utils.hpp"

// A simple structure to store inside our allocated memory to verify integrity
struct TestData
{
    uint32_t processId;
    uint32_t iteration;
    uint64_t magic;
};

void RunAllocatorStress(mem::ShmAlloc* allocator, int processIndex)
{
    // Use a unique seed per process for variety in allocation sizes
    std::mt19937 generator(static_cast<unsigned int>(getpid()));
    std::uniform_int_distribution<size_t> sizeDist(8, 1024 * 1024); // 8B to 1MB

    const int iterations = 5000;
    std::vector<void*> activeAllocations;
    activeAllocations.reserve(iterations);

    for (int i = 0; i < iterations; ++i)
    {
        size_t requestSize = sizeDist(generator);
        void* ptr = allocator->alloc(requestSize);

        if (ptr != nullptr)
        {
            // Verify alignment (assuming 8-byte default)
            if (reinterpret_cast<uintptr_t>(ptr) % 8 != 0)
            {
                std::cerr << "Process " << processIndex << " error: Miss-aligned pointer!" << std::endl;
                _exit(1);
            }

            // Write verification data
            TestData* data = static_cast<TestData*>(ptr);
            data->processId = static_cast<uint32_t>(processIndex);
            data->iteration = static_cast<uint32_t>(i);
            data->magic = 0xABCDEFFF12345678ULL;

            activeAllocations.push_back(ptr);
        }

        // Randomly free some memory to create fragmentation
        if (i % 3 == 0 && !activeAllocations.empty())
        {
            size_t idx = i % activeAllocations.size();
            void* toFree = activeAllocations[idx];

            // Verify before freeing
            TestData* data = static_cast<TestData*>(toFree);
            if (data->magic != 0xABCDEFFF12345678ULL || data->processId != (uint32_t)processIndex)
            {
                std::cerr << "Process " << processIndex << " error: Memory corruption detected!" << std::endl;
                _exit(1);
            }

            allocator->free(toFree);
            activeAllocations.erase(activeAllocations.begin() + idx);
        }
    }

    // Final cleanup
    for (void* ptr : activeAllocations)
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

    if (myAlloc == nullptr)
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

    if (success)
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