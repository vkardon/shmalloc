## ShmAlloc: High-Performance Non-Blocking C++ Shared Memory Allocator

## Introduction

`ShmAlloc` is a specialized C++ memory allocator designed for high-concurrency applications using `fork()` for parallelism. It provides a massive, contiguous virtual memory pool where child processes can allocate data concurrently without the overhead of traditional synchronization locks, while the parent retains zero-copy access to all results.

> [!NOTE]
> While the core logic is stable and follows strict C++ standards, the project is currently in an **Active Development/Alpha** phase and has not yet been deployed in a high-stress production environment.

## Key Features

* **Virtual Shared Memory Pool:** Reserves a contiguous memory block using `mmap(MAP_SHARED)` that is inherited by child processes via `fork()`.
* **Lock-Free Concurrency:** A standout feature\! Optimized for parallel workloads. Children can allocate memory from the shared pool without needing mutexes or heavy atomic synchronization.
* **Parental Consolidation:** Once worker processes complete their tasks, the parent retains direct, pointer-based access to all memory allocated by children.
* **Zero-Copy Architecture:** Eliminates the need for costly data transfers (sockets, pipes, or shared memory copying) between parent and children.
* **Simplified Usage:** Abstracts away complex low-level shared memory and synchronization challenges.
* **C++ Native:** Seamlessly integrates with C++ projects.

---

## Architecture

Unlike traditional allocators that use a global lock or complex thread-local heaps, `ShmAlloc` leverages the behavior of `fork()` and virtual memory mapping to provide a "syncless" allocation experience. It is ideal for parallel computing, data processing pipelines, and scenarios where a parent needs to efficiently collect results from child-allocated data.

  * **Performance:** Achieve high-throughput concurrent allocations in child processes due to its non-blocking design.
  * **Efficiency:** Avoid costly data transfers (like IPC or serialization) between parent and children for shared data.
  * **Simplicity:** Streamlines common multi-process memory patterns, reducing boilerplate and potential for bugs.

`ShmAllocator` is ideal for parallel computing, data processing pipelines, and scenarios where a parent needs to efficiently collect results from child-allocated data.


| Feature | Standard `malloc` | Typical SHM Libs | **shmalloc** |
| :--- | :--- | :--- | :--- |
| **Visibility** | Private to process | Explicit segments | **Shared by Default** |
| **Sync Overhead** | Low (Thread-local) | High (Inter-process locks) | **Zero (Non-blocking)** |
| **Data Transfer** | Manual (Copying) | Manual (Offsets/Shm) | **Zero-Copy Access** |

---

## Usage Example (Conceptual)

```cpp
#include "shmalloc.hpp" // Your main header
#include <sys/wait.h>
#include <iostream>

struct Checksum
{
    pid_t pid{0};
    uint32_t value{0};
};

int main() 
{
    // Initialize ShmAlloc with a 128MB virtual address range
    constexpr size_t size = 1024 * 1024 * 128;
    mem::ShmAlloc* alloc = mem::ShmAlloc::Create("ParallelAllocator", size);
    if(!alloc)
        return EXIT_FAILURE;

    // Allocate an array of results in shared memory for 4 children.
    const int numChildren = 4;
    Checksum** cksumList =  new (alloc) Checksum*[numChildren]{};
    if(!cksumList)
        return EXIT_FAILURE;

    // Spawning children
    std::cout << "\n--- Parent Forking " << numChildren << " Children... ---" << std::endl;
    
    for(int i = 0; i < numChildren; ++i)
    {
        pid_t pid = fork();

        if(pid == 0) 
        { 
            // --- Child Process ---
            // Seed with PID and sleep up to 200ms to demonstrate out-of-order execution
            srand(getpid());
            usleep((rand() % 200) * 1000);

            // Each child allocates its own Checksum object.
            // ShmAlloc's non-blocking lock ensures this is process-safe.
            Checksum* cksum = new (alloc) Checksum;

            // Each child performs a simulated calculation
            cksum->pid = getpid();
            cksum->value = (0x12345 * (i + 1)) ^ 0xFFFFFFFF;

            // Store the address in the shared array for the parent to access.
            cksumList[i] = cksum;

            std::cout << "[Child " << i << "] PID " << cksum->pid << " wrote checksum: 0x"
                      << std::hex << cksum->value << std::dec << std::endl;
            
            exit(EXIT_SUCCESS); // The child process is done
        }
        else if(pid < 0)
        {
            perror("fork failed");
            return EXIT_FAILURE;
        }
    }

    // Parent Process: Wait for all children to finish
    for(int i = 0; i < numChildren; ++i)
    {
        wait(nullptr);
    }

    // Parent reports the collected data
    std::cout << "\n--- Parent Multi-Child Report ---" << std::endl;

    for(int i = 0; i < numChildren; ++i)
    {
        Checksum* cksum = cksumList[i];
        if(cksum)
        {
            std::cout << "Slot [" << i << "]: PID " << cksum->pid 
                    << ", Checksum: 0x" << std::hex << cksum->value << std::dec << std::endl;
            delete cksum; 
        }
    }

    // Clean up
    delete [] cksumList;

    return EXIT_SUCCESS;
}
```

## License

This project is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. 

### Open Source Use
You are free to use, study, and modify this library for open-source projects, provided that any project using these headers is also licensed under the AGPL-3.0 (or a compatible license) and makes its source code available to its users.

### Commercial & Proprietary Use
Because `shmalloc` is an AGPL-licensed C++ library, including these headers in a proprietary or networked application typically requires you to release your entire project's source code under the same license. If you wish to use this library in a commercial or closed-source environment without these restrictions, a separate commercial license is required. For custom licensing inquiries, private support, or high-performance integration consulting, please contact me via the email address listed on my **[GitHub Profile](https://github.com/vkardon)**.

-----
