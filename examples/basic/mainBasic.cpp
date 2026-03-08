//
// main.cpp
//
#include <iostream>
#include "shmalloc.hpp"
#include "utils.hpp"

class Data
{
public:
    Data(int _id) { id = _id; }
    ~Data() = default;
    int id{-1};
    Data* next{0};
};

//
// Actual test routine
//
bool TestAllocator(int childrenCount, int allocCount)
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
    Data** dataArr = new (al) Data*[childrenCount]{};

    std::cout << "Parent: Spawning " << childrenCount << " child processes" << std::endl;

    auto childProc = [&](int childIndex) 
    {
        // Running as a child.
        // Allocate a Data object. Then, link the current Data object
        // to the previous one, forming the chain (which can be traversed later
        // to validate the allocations).
        Data* prev = nullptr;
        for(int i = 0; i < allocCount; i++)
        {
            Data* data = new (al) Data(i);  // Allocate a Data object

            // Link the current Data object to the previous one
            prev ? prev->next = data : dataArr[childIndex] = data;
            prev = data;
        }
    };

    // Fork children...
    if(!ForkAndWait(childrenCount, childProc))
    {
        std::cerr << "Failed to fork or some of the children processes returned error" << std::endl;
        return false;
    }

    std::cout << "Parent: All children completed" << std::endl;

    // Access children't data
    size_t totalAllocations = 0;
    for(int childIndex = 0; childIndex < childrenCount; childIndex++)
    {
        std::cout << "Child " << childIndex << ": reading allocations..." << std::endl;

        for(Data* data = dataArr[childIndex]; data; data = data->next)
        {
            std::cout << "Child " << childIndex << ": Data.id=" << data->id << std::endl;
            totalAllocations++;
        }
    }
    std::cout << "Parent: Total allocations count " << totalAllocations << std::endl;

    return true;
}

int main()
{
    int childrenCount = 4;
    std::size_t allocCount = 10;
 
    TestAllocator(childrenCount, allocCount);
    return 0;
}

