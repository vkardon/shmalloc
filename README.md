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

int main() 
{
    // NOTE: Error handling omitted for brevity

    // Parent initializes ShmAllocator with a large shared memory pool
    constexpr size = (1024 * 1024 * 1024 * 256); // 256GB address range
    std::unique_ptr<shm::ShmAllocator> alloc(shm::ShmAllocator::Create("MyAllocator", size));

    // Declare pointer for child process to assign
    char* data = nullptr;

    // Fork child process(es)
    pid_t pid = fork();

    if (pid == 0) 
    { 
        // Child process
        // Child can allocate memory concurrently without locks
        data = new (alloc) char [2048];
        // ... use data ...

        exit(0);
    } 
    else if (pid > 0) 
    { 
        // Parent process. Wait for children
        waitpid(pid, nullptr, 0);

        // Parent can now directly access memory allocated by child
        // Use data allocated by child
        // ..
    }

    return 0;
}
```

## License

This project is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. 

### Open Source Use
You are free to use, study, and modify this library for open-source projects, provided that any project using these headers is also licensed under the AGPL-3.0 (or a compatible license) and makes its source code available to its users.

### Commercial & Proprietary Use
Because `shmalloc` is an AGPL-licensed C++ library, including these headers in a proprietary or networked application typically requires you to release your entire project's source code under the same license. If you wish to use this library in a commercial or closed-source environment without these restrictions, a separate commercial license is required. For custom licensing inquiries, private support, or high-performance integration consulting, please contact me via the email address listed on my **[GitHub Profile](https://github.com/vkardon)**.

-----
