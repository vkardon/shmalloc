//
// shmalloc.hpp
//
#ifndef __SHMALLOC_HPP__
#define __SHMALLOC_HPP__

#include <sys/mman.h>   // For mmap()
#include <string.h>     // For strerror()
#include <unistd.h>     // For pid_t
#include <pthread.h>    // For pthread_atfork, pthread_once_t, pthread_once, PTHREAD_ONCE_INIT
#include <cassert>      // assert()
#include <cstdio>       // For perror()
#include <cstdlib>      // For exit, EXIT_FAILURE
#include <iostream>     // For std::endl
#include <string>
#include <memory>
#include <thread>       // For std::this_thread::yield()
#include <signal.h>     // For kill()
#include "alloc.hpp"

namespace mem {

// Helper macros to unify logging of information, debug and error messages
// TODO: Convert to inline functions

// Strips the '/path/' prefix from the file name
constexpr const char* __fname__(const char* file, int i)
{
    return (i == 0) ? (file) : (*(file + i) == '/' ? (file + i + 1) : __fname__(file, i - 1));
}

#define memLOG(stream, type, msg) do { \
    stream << "[" << getpid() << "] " << type \
           << " [" << mem::__fname__(__FILE__, sizeof(__FILE__)-1) << ":" << __LINE__ << "] " \
           << __func__ << ": " << msg << std::endl; \
} while(0)

#define memVERBOSE(msg) if(verbose)         memLOG(std::cout, "DEBUG", msg)
#define memDEBUG(msg)   if(mEnableDebugLog) memLOG(std::cout, "DEBUG", msg)
#define memINFO(msg)    if(mEnableInfoLog)  memLOG(std::cout, "INFO", msg)
#define memERROR(msg)                       memLOG(std::cerr, "ERROR", msg)
#define memOUT(msg)                         memLOG(std::cout, "OUT", msg)

// PID of the current process (could be a parent of a child)
// NOTE: Must 'inline' to ensure a single definition across all translation units (C++17 and up)
inline pid_t gCurrentPid = getpid();

// This function will be called in the newly forked child process, right after fork().
inline void OnChildPostFork()
{
    // fprintf(stdout, ">>> Child PID %d\n", getpid());
    gCurrentPid = getpid(); // Update allocator with the current process id

    // Seed the random number generator for the new process.
    // We mix PID and Time to ensure that even if two processes fork 
    // at the exact same microsecond, they likely get different seeds.
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srandom((unsigned int)(gCurrentPid ^ ts.tv_nsec ^ ts.tv_sec));
    // memOUT("DEBUG", "gCurrentPid=" << gCurrentPid);
}

// Performs the actual pthread_atfork registration.
inline void RegisterChildPostForkImpl()
{
    if(pthread_atfork(NULL, NULL, OnChildPostFork) != 0)
    {
        perror("pthread_atfork registration failed for child handler");
        exit(EXIT_FAILURE); // Critical error if this fails, so exit.
    }
//    fprintf(stderr, "PID %d: pthread_atfork child handler successfully registered.\n", getpid());
}

// NOTE: Must 'inline' to ensure a single definition across all translation units (C++17 and up)
inline pthread_once_t registerOnce = PTHREAD_ONCE_INIT;

// This inline global variable will ensure its constructor is called exactly once
// across the program, triggering the atfork registration.
// NOTE: Must 'inline' to ensure a single definition across all translation units (C++17 and up)
struct AtForkInitializer 
{
    AtForkInitializer() { pthread_once(&registerOnce, RegisterChildPostForkImpl); }
};
inline AtForkInitializer g_atForkInitializer;

class ShmAlloc : public Alloc
{
    struct BlockHead
    {
        char* ptr{nullptr};         // Next free chunk
        std::size_t blockSize{0};   // Internal size of chunk in bytes
    };

    struct SharedData
    {
        // We place the lock variables at the top and align them to a 64-byte
        // boundary (a standard CPU cache line). This prevents "False Sharing"
        // where the CPU fights itself over nearby data.
        // The 'lock' variable serves two purposes:
        //   0        => Unlocked
        //   Non-zero => Locked (value is the PID of the owner)
        alignas(64) pid_t lock{0};

        char* top{nullptr};     // Last actually available address process-wide

        // victor test - TODO: Temporarily collecting statistics
        struct BlockStats
        {
            std::size_t memRequested{0};    // Total memory requested for this block
            std::size_t allocCount{0};      // Total allocation count for this block
        };
        BlockStats* blockStats{nullptr};
        std::size_t chainDust{0};       // 8-byte page headers and block ends.
        std::size_t dumpedMemory{0};    // Memory that is literally thrown away.
        // victor test end - TODO: Temporarily collecting statistics
    };

protected:
    // The constructor is protected; this enforces 
    // the use of the static Create() method for creating new allocator
    ShmAlloc(const std::string& name, int flags, void* start, void* stop);

    // Delete the rest of constructors
    ShmAlloc() = delete;
    ShmAlloc(const ShmAlloc&) = delete;
    ShmAlloc(ShmAlloc&&) = delete;
    ShmAlloc& operator=(const ShmAlloc&) = delete;
    ShmAlloc& operator=(ShmAlloc&&) = delete;

public:
    virtual ~ShmAlloc();

    // Create a fixed-position allocator using an address range
    static ShmAlloc* Create(const std::string& name, void* start, void* stop, bool verbose=false);
    static ShmAlloc* Create(const std::string& name, uint64_t start, uint64_t stop, bool verbose=false);

    // Create a position-independent allocator that uses a specified size
    static ShmAlloc* Create(const std::string& name, std::size_t size, bool verbose=false);

    // Public Allocator class overrides
    virtual void* alloc(std::size_t size) override;
    virtual void free(void* ptr) override;
    virtual const char* getName() const override { return mName.c_str(); }
    virtual void* getStart() const override { return mStart; }
    virtual void* getStop() const override { return mStop; }

    bool contains(const void* addr) const;  // Check if address is within managed range
    void* getBase() const { return mBase; } // Memory address to be filled next
    std::size_t getUsed() const;            // Memory actually used
    std::size_t getFree() const;            // Memory allocated but free
    std::size_t getVSZ() const;             // Get Virtual Memory Size (VSZ)
    std::size_t getRSS() const;             // Get Resident Set Size (RSS)

    // victor test - TODO: Temporarily collecting statistics
    bool getTotals(std::size_t& totalRequested, std::size_t& totalAllocated, std::size_t& totalAllocCount, FILE* out = nullptr) const;

    // Report memory usage and statistics to the specified output
    void audit(FILE* out = stdout, const std::string& msg = "");

    // Diagnostic Methods (Enable/disable memINFO and memDEBUG loggers)
    void enableInfoLog(bool enable) { mEnableInfoLog = enable; }
    void enableDebugLog(bool enable) { mEnableDebugLog = enable; }
    
    bool testFindSlot();    // Test the effectiveness of findSlot()

private:
    // Private Methods.
    void initSlots();
    int findSlot_orig(std::size_t size) const;
    int findSlot(std::size_t size) const;
    char* getMemory(std::size_t size);
    void recycleMemory(char* ptr, std::size_t size);
    bool postForkReset(); // Post-fork reset by child process

    // Force program termination with a specific error value,
    // intended to be readily visible during core dump analysis.
    void crashWithCoreDump(uint64_t error) const;

    // Data Members.
    SharedData* mSharedData{nullptr};

    size_t mSysPageSize{0}; // The system page size
    pid_t mOwnerPid{0};     // PID of the process that own the allocator (parent or child)
    std::string mName;      // Name of this allocator.
    int mFlags{0};          // Allocator option flags.

    char* mStart{nullptr};  // Beginning address of allocation
    char* mStop{nullptr};   // End address of allocation
    char* mBase{nullptr};   // Current available address (for this process)
    char* mTop{nullptr};    // Last actually available address (for this process)

    int mFirstLargeSlot{0};
    int mMaxSlots{0};

    std::unique_ptr<BlockHead[]> mBlocks;

    // Memory usage statistics & diagnostics
    // Enable/disable memINFO and memDEBUG loggers
    bool mEnableInfoLog{false};
    bool mEnableDebugLog{false};

    // The following two statements define the number of bytes that shmalloc
    // treats as a page. Shmalloc never internally moves space in quanta smaller
    // than this. Allocations larger than one page are expressed as integer
    // multiples of this value.
    static constexpr uint64_t MALLOCPAGE = 8192;                // 0x2000 --> 10 0000 0000 0000 (8KB)
    static constexpr uint64_t MALLOCPAGE_MASK = (MALLOCPAGE-1); // 0x1FFF -->  1 1111 1111 1111

    // We are using "size classes" pattern to manage our allocationsis.
    // This is a common and highly effective strategy, particularly for 
    // managing many small and medium-sized memory allocations.

    // Objects up to 2KB are allocated in 8-byte or quanta for maximum
    // efficiency. We don't know what alignment the operating system
    // is assuming, so we choose 8-byte as the safest available.
    // Note: We also support experimental 4-byte or quanta enabled by ALIGN4BYTES flag
    static constexpr std::size_t MAXSMALL = 0x0800;            // = 2048 - Max size of a small object
    static constexpr int MAXSMALLSLOTS_8 = (MAXSMALL >> 3);    // 8-bytes quanta: MAXSMALL / 2^3 = 2048/8 = 256
    static constexpr int MAXSMALLSLOTS_4 = (MAXSMALL >> 2);    // 4-bytes quanta: MAXSMALL / 2^2 = 2048/4 = 512

    // Objects bigger than MAXSMALL (2KB) are stored in "half power of 2" pattern.
    // The max size of the object depends on number of slots.
    // For example, it will be 6GB with 43 slots.
    static constexpr int MAXLARGESLOTS = 43;

    // The logarithm base 2 of MAXSMALL (2048), used by alloc()
    static constexpr int LARGELOG  = 11;  // 2 ^ 11 = 2048

    // The increment amount to establish a new memory base, used by getMemory()
    static constexpr std::size_t EXTEND_MEM_SIZE = 1024 * 1024;          // 1 MB
    // static constexpr std::size_t EXTEND_MEM_SIZE = 1024 * 1024 * 4;      // 4 MB
    // static constexpr std::size_t EXTEND_MEM_SIZE = 1024 * 1024 * 16;     // 16 MB
    // static constexpr std::size_t EXTEND_MEM_SIZE = 1024 * 1024 * 32;     // 32 MB

    // Round up to the nearest multiple of MALLOCPAGE
    uint64_t ROUND_UP_TO_MALLOCPAGE(size_t val) const
    {
        return (static_cast<uint64_t>(val) + MALLOCPAGE_MASK) & ~MALLOCPAGE_MASK;
    }

    // Calculate padding to nearest multiple of MALLOCPAGE
    uint64_t PADDING_TO_MALLOCPAGE(void* val) const
    {
        return (-(reinterpret_cast<uint64_t>(val) & MALLOCPAGE_MASK)) & MALLOCPAGE_MASK;
    }
};
// End of ShmAlloc class

// Spin lock class to synchronize access to the allocator's shared data. 
// It uses Atomic PID-tagging to detect and recover from crashed process locks 
// and a three-stage backoff to minimize wasted CPU cycles and cache thrashing.
class ShmLock
{
public:
    ShmLock(pid_t* lockAddr, int waitMs, pid_t pidIn)
    {
        if(lockAddr == nullptr)
            return;

        pid_t groupPid = getpgrp();
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int spinCount = 0;

        while(true)
        {
            // Test-and-Test-and-Set: 
            // Check if the lock is free (0) before attempting CAS
            if(__atomic_load_n(lockAddr, __ATOMIC_RELAXED) == 0)
            {
                // Atomic Swap: If lock is 0, set it to our PID.
                // This combines "grabbing the lock" and "setting owner" into one step.
                pid_t expected = 0;
                if(__atomic_compare_exchange_n(lockAddr, &expected, pidIn, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                {
                    // Success: We now own the lock.
                    targetLock = lockAddr;
                    ownerPid = pidIn;
                    break;
                }
            }

            // Recovery Logic: Check if the current owner is a "ghost"
            pid_t currentOwner = __atomic_load_n(lockAddr, __ATOMIC_RELAXED);
            if(currentOwner != 0 && currentOwner != pidIn)
            {
                bool shouldReset = false;

                if(kill(currentOwner, 0) == -1 && errno == ESRCH)
                {
                    shouldReset = true;
                }
                else if(getpgid(currentOwner) != groupPid)
                {
                    shouldReset = true;
                }

                if(shouldReset)
                {
                    // Atomic Reset: Only clear if the dead PID is still the owner.
                    // This prevents us from accidentally unlocking a new, valid owner.
                    pid_t expectedDead = currentOwner;
                    __atomic_compare_exchange_n(lockAddr, &expectedDead, 0, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);

                    // Immediately retry acquisition
                    continue;                
                }
            }

            // Timeout Check
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsedMs = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
            
            if(elapsedMs >= waitMs)
                break;  // Timeout reached; targetLock remains nullptr

            // Multi-stage Backoff
            if(spinCount < 100)
            {
                // Stage A: High-speed CPU hint
#if defined(__i386__) || defined(__x86_64__)
                __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
                asm volatile("yield" ::: "memory");
#else
                std::this_thread::yield();
#endif
            }
            else if(spinCount < 200)
            {
                // Stage B: Soft yield to OS scheduler
                std::this_thread::yield();
            }
            else
            {
                // Stage C: Hard sleep to save power/CPU cycles
                useconds_t delay = (useconds_t)((random() % 1000) + 500);
                usleep(delay);
            }

            spinCount++;
        }
    }

    ~ShmLock()
    {
        if(targetLock != nullptr)
        {
            // Atomic Release: Only set to 0 if we still actually own it.
            // This protects against cases where a recovery reset happened 
            // to us during a massive process stall.
            pid_t expected = ownerPid;
            __atomic_compare_exchange_n(targetLock, &expected, 0, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        }
    }

    explicit operator bool() const { return (targetLock != nullptr); }

    ShmLock(const ShmLock&) = delete;
    ShmLock(ShmLock&&) = delete;
    ShmLock& operator=(const ShmLock&) = delete;
    ShmLock& operator=(ShmLock&&) = delete;

private:
    pid_t* targetLock{nullptr};
    pid_t ownerPid{0};
};
// End of ShmLock class

// Define allocator flags
#define ALIGN4BYTES         0x00000001          // Use 4-bytes alignment for small slots
// ... define more flags as needed ...

// Helper macros to verify MALLOCPAGE alignment (only for Debug build)
#ifndef NDEBUG
    // Verify that val is MALLOCPAGE-aligned (val is on page boundary)
    #define VERIFY_MALLOCPAGE_ALIGNED(val) do{ \
        if((reinterpret_cast<uint64_t>(val) & MALLOCPAGE_MASK) != 0) \
        { \
            memERROR("val " << reinterpret_cast<void*>(val) << " is NOT MALLOCPAGE-aligned"); \
            /*audit(true);*/ \
            crashWithCoreDump(0xa10000bad1); \
        } \
    } while(0)

    // Verify that val is NOT MALLOCPAGE-aligned (val is NOT on page boundary)
    #define VERIFY_NOT_MALLOCPAGE_ALIGNED(val) do{ \
        if(val && (reinterpret_cast<uint64_t>(val) & MALLOCPAGE_MASK) == 0) \
        { \
            memERROR("val " << reinterpret_cast<void*>(val) << " is MALLOCPAGE-aligned"); \
            /*audit(true);*/ \
            crashWithCoreDump(0xa10000bad2); \
        } \
    } while(0)
#else
    #define VERIFY_MALLOCPAGE_ALIGNED(ptr) do {} while(0) // No-op in Release
    #define VERIFY_NOT_MALLOCPAGE_ALIGNED(ptr) do {} while(0) // No-op in Release
#endif

// Helper class for printing a memory address in hexadecimal format (by casting to void*).
template <typename T>
struct ADDR
{
    const T value{};
    constexpr explicit ADDR(T p) : value(p) {}
    friend std::ostream& operator<<(std::ostream& os, const ADDR& a)
    {
        if constexpr (std::is_pointer_v<T>)
        {
            return os << (a.value == nullptr ? "0x0" : static_cast<const void*>(a.value));
        }
        else
        {
            return os << (a.value == 0 ? "0x0" : reinterpret_cast<const void*>(static_cast<uintptr_t>(a.value)));
        }
    }
};

// Helper class for printing a size in hexadecimal format (by casting to void*).
template <typename T>
struct SIZE
{
    const T value{};
    constexpr explicit SIZE(T v) : value(v) {}
    friend std::ostream& operator<<(std::ostream& os, const SIZE& s)
    {
        if constexpr (std::is_pointer_v<T>)
        {
            return os << reinterpret_cast<uintptr_t>(s.value) << "(" << reinterpret_cast<void*>(s.value) << ")";
        }
        else
        {
            return os << s.value << "(" << reinterpret_cast<void*>(s.value) << ")";
        }
    }
};

inline ShmAlloc::ShmAlloc(const std::string& name, int flags, void* start, void* stop)
{
    // Get the system page size
    if(long sysPageSize = sysconf(_SC_PAGE_SIZE); sysPageSize == -1)
    {
        std::string errmsg = strerror(errno);
        memERROR("sysconf(_SC_PAGE_SIZE) failed: " << errmsg << ".");
    }
    else
    {
        mSysPageSize = sysPageSize;
    }

    mOwnerPid = getpid();   // Owner process
    mName = name;
    mFlags = flags;

    mStart = (char*)start;
    mStop = (char*)stop;
    mBase = mStart;         // Current available address (for this process)
    mTop = mStart;          // Last actually available address  (for this process)

    initSlots();

    // Allocate SharedData using our shared memory
    SharedData sharedDataTmp;
    sharedDataTmp.top = mBase;
    mSharedData = &sharedDataTmp;
    mSharedData = new (this) SharedData;    // This is update sharedDataTmp
    mSharedData->top = sharedDataTmp.top;   // This should be the same as mBase

    // victor test - TODO: Temporarily collecting statistics
    mSharedData->blockStats =  new (this) SharedData::BlockStats[mMaxSlots];
}

inline ShmAlloc::~ShmAlloc()
{
    memINFO("In destructor of Allocator '" << getName() << "'");

    // Cleanup: Unmap memory and mark the memory range as free
    if(mStart == mStop)
        return; // Nothing to cleanup

    // This allocator no longer handles the memory range.
    // Set the addresses from start to top as free.
    SetAllocator(this, false);

    std::size_t mapped = mStop - mStart;

    memINFO("Allocator '" << getName() << "' unmapping "
            << SIZE(mapped) << " bytes at " << ADDR(mStart));

    if(munmap(mStart, mapped) != 0)
    {
        std::string errmsg = strerror(errno);
        memERROR("munmap failed, addr = " << ADDR(mStart)
                 << ", size = " << SIZE(mapped) << ": " << errmsg << ".");
    }
}

//
// Objects up to 2KB (MAXSMALL) are allocated in 8-byte or quanta. 
// Objects bigger than MAXSMALL (2KB) are stored in "half power of 2" pattern.
//
// -----------------------------------------------------------------
// Request Size | Logic Type  | Spacing Max | Internal Waste
// -----------------------------------------------------------------
// 0 - 2 KB     | Linear      | 8 bytes     | < 8 bytes
// 2 KB - 6 GB	| Logarithmic |	1.5 × 2^n   | ~25%
// -----------------------------------------------------------------
//
inline void ShmAlloc::initSlots()
{
    bool use4bytesAlignment = (mFlags & ALIGN4BYTES);
    mFirstLargeSlot = (use4bytesAlignment ? MAXSMALLSLOTS_4 : MAXSMALLSLOTS_8);
    mMaxSlots = mFirstLargeSlot + MAXLARGESLOTS;
    
    mBlocks.reset(new (std::nothrow) BlockHead[mMaxSlots]{});
    if(!mBlocks)
    {
        memERROR("Pid " << getpid() << ", allocator '" << mName << "' - "
                 "Out of memory allocating " << (sizeof(BlockHead) * mMaxSlots) << " bytes");
        exit(EXIT_FAILURE); // Critical error if this fails, so exit.
    }

    // Objects up to 2KB are allocated in 8-byte or quanta for maximum
    // efficiency. We don't know what alignment the operating system
    // is assuming, so we choose 8-byte as the safest available.
    // Note: We also support experimental 4-byte or quanta enabled by ALIGN4BYTES flag

    // Init blockSize for small blocks.
    // For example, for 8 bytes alignment, blockSize will be set to
    // 0x008, 0x010, 0x018, 0x020, ..., 0x7f0, 0x7f8, 0x800
    //     8     16     32     40        2032   2040   2048
    std::size_t sz = 0;
    int increment = (use4bytesAlignment ? 4 : 8);
    for(int i = 0; i < mFirstLargeSlot; i++)
    {
        sz += increment;
        mBlocks[i].blockSize = sz;
    }
    assert(sz == MAXSMALL);

    // Objects bigger than MAXSMALL (2KB) are stored in "half power of 2" pattern.
    // The max size of the object depends on number of slots.
    // For example, it will be 6GB with 43 slots.
    //
    // 1. The p += i % 2 creates a sequence where p holds for two iterations, then increments. 
    //
    // 2. The sequence of added values to sz is:
    // 0x400, 0x400, 0x800, 0x800, 0x1000, 0x1000, 0x2000, 0x2000, ...
    //
    // And the sz value (which becomes blockSize) accumulates these. 
    // The result is a set of "size classes" that have a denser packing at smaller sizes 
    // and then spread out logarithmically. This is a common strategy to efficiently manage 
    // memory requests by rounding them up to the nearest available block size. 
    // The $X000 values are pure powers of two, and the $Y000 values are often intermediate points (e.g., 1.5):
    //
    // 0x0800              (2,048 bytes) - A direct power of two  --> MAXSMALL
    // 0x0c00              (3,072 bytes) - An intermediate point (x 1.5)
    // 0x1000              (4,096 bytes) - A direct power of two
    // 0x1800              (6,144 bytes) - An intermediate point
    // 0x2000              (8,192 bytes) - A direct power of two
    // 0x3000             (12,288 bytes) - An intermediate point
    // 0x4000             (16,384 bytes) - A direct power of two
    // 0x6000             (24,576 bytes) - An intermediate point
    // 0x8000             (32,768 bytes) - A direct power of two
    // ...
    // 0x0c0000000 (3,221,225,472 bytes) - An intermediate point
    // 0x100000000 (4,294,967,296 bytes) - A direct power of two
    // 0x180000000 (6,442,450,944 bytes) - An intermediate point  --> 6 GB exactly

    int p = 10;  // 2^10 = 1 << 10 = 1024
    for(int i = mFirstLargeSlot; i < mMaxSlots; i++)
    {
        sz += ((std::size_t)1 << p);
        mBlocks[i].blockSize = sz;
        p += i % 2; // p holds for two iterations, then increments
    }
}

//
// Find memory slot corresponding to desired side
//
inline int ShmAlloc::findSlot_orig(std::size_t size) const
{
    if(size == 0)
        size = 8; // force to 8 bytes

    // Normalization: Decrement size to handle "edge" cases at bucket boundaries. 
    // This ensures that requests exactly equal to a bucket's maximum (e.g., 8, 16, 24, 1024) 
    // map to the correct index. For example, without this, an 8-byte request (8 >> 3) 
    // would index into the 16-byte slot, wasting an entire memory block for every 
    // allocation hitting a size-class boundary.
    size--;

    // Get the block table corresponding to the size of the object.
    int indx = 0;

    if(size < MAXSMALL)         // exact power of 2 is last 
    {
        // An index to a small objects table is the quotient obtained by dividing 
        // the requested size by a quanta.        
        if(mFlags & ALIGN4BYTES)
            indx = size >> 2; // = size / 2^2 = size / 4  (4 bytes quanta)
        else
            indx = size >> 3; // = size / 2^3 = size / 8  (8 bytes quanta)
    }
    else
    {
        // The explanation of this code is below.
        // // determine log 2 of size
        // int log = -2;
        // size >>= LARGELOG - 1;            // toss away known bits
        // while(size > 3)              // toss then test
        // {
        //     log += 2;               // increment if greater
        //     size >>= 1;
        // }
        // block = &mBhLarge[log + size];      // pull table entry

        // First is the normalization step. We know our smallest bucket is 2^11 (2048). 
        // By right-shifting size by 10 bits, we are effectively dividing it by 2^10 (1024).
        // This shifts our "window" of analysis. Instead of dealing with values like 2048, 3072, 4096, 
        // we're now dealing with smaller, more manageable numbers:
        //
        // If size is 2048 (2^11), tmp becomes 2048 >> 10 = 2.
        // If size is 3072 (1.5 times 2^11), tmp becomes 3072 >> 10 = 3.
        // If size is 4096 (2^12), tmp becomes 4096 >> 10 = 4.
        // If size is 6144 (1.5 times 2^12), tmp becomes 6144 >> 10 = 6.
        // If size is 8192 (2^13), tmp becomes 8192 >> 10 = 8.
        //
        // And so on... tmp will always be a power of 2 (2, 4, 8, ...) or 1.5 times a power of 2 (3, 6, 12, ...).
        std::size_t tmp = size >> (LARGELOG - 1); // toss away known bits (LARGELOG is 11, so size >> 10)

        // The log variable acts as an accumulator for the "base" part of our index. 
        // It starts at -2 as an offset to make the final log + tmp calculation yield 
        // the correct 0-based index for your sequence. 
        int log = -2;

        // Note: tmp after this shift alternates between 2 * (some power of 2) and 
        // 3 * (some power of 2). This tmp now holds the key to identifying which 
        // "pair" of buckets (2^N or 1.5 * 2^N) size falls into.

        // The while(tmp > 3) loop is the core of the logarithmic mapping. 
        // It processes tmp as long as it's larger than 3.
        // - tmp values of 2 or 3 are considered "base cases" that don't need further shifting. 
        //   They represent the first pair of buckets in our normalized sequence.
        // - Any tmp greater than 3 (e.g., 4, 6, 8, 12, ...) means size corresponds 
        //   to a bucket beyond the initial 2^11 and 1.5 times 2^11 range.
        while(tmp > 3)        // toss then test
        {
            // Purpose: These two lines work together to scale log and reduce tmp.

            // log += 2 :
            //    For every time tmp is halved, we add 2 to log. Why 2? Because our indices 
            //    jump by 2 for each full power-of-two group.
            //     - Index 0 (2^11) to Index 2 (2^12) is a jump of +2.
            //     - Index 2 (2^12) to Index 4 (2^13) is a jump of +2.
            //    This ensures log accurately tracks the "base" index corresponding to the power-of-two level.
            log += 2;         // increment if greater

            // tmp >>= 1 :
            //    This halves tmp in each iteration. This effectively counts how many times 
            //    tmp needs to be divided by 2 to get back to the 2 or 3 range. Each division 
            //    by 2 in tmp corresponds to moving to the next higher power of 2 in our original 
            //    sequence.
            tmp >>= 1;
        }

        // The final index calculation.
        // After the while loop, tmp will be either 2 or 3.
        // - If tmp is 2, it means the size (after normalization and shifting) 
        //   corresponds to a 2^N bucket.
        // - If tmp is 3, it means the size (after normalization and shifting) 
        //   corresponds to a 1.5 times 2^N bucket.
        //
        // The log variable has accumulated the necessary offset based on how many 
        // "power-of-two generations" (tmp >>= 1) we passed through.
        //
        // Adding log and tmp precisely yields the 0-based index for the bucket that 
        // is greater than or equal to size.
        indx = mFirstLargeSlot + (log + tmp);
    }

    return indx;
}

//
// Find memory slot corresponding to desired side
//
inline int ShmAlloc::findSlot(std::size_t size) const
{
    if(size == 0)
        size = 8;

    // Normalization: Decrement size to handle "edge" cases at bucket boundaries. 
    // This ensures that requests exactly equal to a bucket's maximum (e.g., 8, 16, 24, 1024) 
    // map to the correct index. For example, without this, an 8-byte request (8 >> 3) 
    // would index into the 16-byte slot, wasting an entire memory block for every 
    // allocation hitting a size-class boundary.
    size--;

    // Get the block table corresponding to the size of the object.
    int indx = 0;

    if(size < MAXSMALL)         // exact power of 2 is last 
    {
        // An index to a small objects table is the quotient obtained by dividing 
        // the requested size by a quanta.        
        if(mFlags & ALIGN4BYTES)
            indx = size >> 2; // = size / 2^2 = size / 4  (4 bytes quanta)
        else
            indx = size >> 3; // = size / 2^3 = size / 8  (8 bytes quanta)
    }
    else // Large Slot Logic (O(1) complexity) -
    {
        // Find the highest set bit (the floor of log2).
        // __builtin_clzl counts leading zeros. Subtracting from 63 (for 64-bit size_t) 
        // gives the index of the highest bit. (e.g., 4096 has bit 12 set).
        int highBit = 63 - __builtin_clzl(size);

        // Determine if we are in the "power of 2" bucket or the "1.5 * power of 2" bucket.
        // We check the bit immediately below the highest bit.
        // If it's set (1), the value is in the 1.5x range.
        int halfStep = (int)((size >> (highBit - 1)) & 1);

        // Calculate the "generation" offset.
        // LARGELOG is 11 (2048). We subtract 10 to normalize the exponent 
        // so that the first large bucket (2048) starts our index count.
        int generation = highBit - (LARGELOG - 1);

        // Final index assembly.
        // Each generation has 2 slots (the power of 2 and the 1.5x intermediate).
        // The -2 offset aligns the result with the start of the large slot array.
        indx = mFirstLargeSlot + (generation * 2) + halfStep - 2;
    }

    return indx;
}

// Test the effectiveness of findSlot():
// Verify findSlot() returns the smallest available block large enough
// to accommodate the requested size.
//
// Note: For a maximum size of ~6.4 GB, this loop will execute approximately
// 6.4 billion times. Depending on CPU speed, this may take anywhere from
// several seconds to a few minutes to complete. This method is intended for
// one-time validation within a heavy test suite.
inline bool ShmAlloc::testFindSlot()
{
    memOUT("Starting findSlot geometry validation...");

    // Iterate through every possible size up to the maximum managed block
    std::size_t maxSupportedSize = mBlocks[mMaxSlots - 1].blockSize;
    
    // We test standard boundaries and some random samples to save time, 
    // or you can do a full linear sweep if maxSupportedSize isn't billions.
    for(std::size_t size = 1; size <= maxSupportedSize; ++size)
    {
        int indx = findSlot(size);

        // Validation A: Bounds Check
        if(indx < 0 || indx >= mMaxSlots)
        {
            memERROR("Size " << size << " mapped to out-of-bounds slot: " << indx);
            return false;
        }

        std::size_t selectedBlockSize = mBlocks[indx].blockSize;

        // Validation B: Does it actually fit?
        if(selectedBlockSize < size)
        {
            memERROR("Size " << size << " mapped to slot " << indx 
                     << " which is too small (" << selectedBlockSize << ")");
            return false;
        }

        // Validation C: Is it the smallest possible fit? (The most important check)
        if(indx > 0)
        {
            std::size_t prevBlockSize = mBlocks[indx - 1].blockSize;
            if(prevBlockSize >= size)
            {
                memERROR("Size " << size << " mapped to slot " << indx 
                         << " (" << selectedBlockSize << "), but could have fit in slot " 
                         << (indx - 1) << " (" << prevBlockSize << ")");
                return false;
            }
        }
        
        // Progress logging for large sweeps
        if(size % 1000000 == 0) 
        {
            memINFO("Validated up to size: " << size);
        }
    }

    memOUT("SUCCESS: All sizes mapped to the optimal bucket index.");
    return true;
}

//
// Allocate a piece of memory of the indicated size from the process-specific range
//
inline void* ShmAlloc::alloc(std::size_t size)
{
    // If we are a child process making very first alloc() call, then perform post-fork
    // reset to make sure we are not using memory in the area already allocated by our mom.
    if(mOwnerPid != gCurrentPid)
    {
        // We must be a child process asking memory for a first time after fork
        if(!postForkReset())
            return nullptr;
        mOwnerPid = gCurrentPid;
    }

    // Get the memory slot correspondint to the size of the object.
    // Check if an object is already available on the appropriate free chain. 
    int blockIndx = findSlot(size);
    BlockHead* block = &mBlocks[blockIndx];
    char* ptr = block->ptr;

    memDEBUG("---> Enter: size=" << SIZE(size) << ", block->blockSize=" << SIZE(block->blockSize) << ", block->ptr=" << ADDR(ptr));

    if(!ptr)
    {
        // There isn't an available object. We need to get more space on this free chain.
        // Add 8 bytes for the chain header and then round up to the next MALLOCPAGE size. 
        // Notice that this mechanism is extremely efficient for small objects but that it
        // becomes much less efficient as we approach the page size. After we pass the page
        // size we become more efficient as the size of the dumped bytes at the end of each
        // allocation becomes small in constrast to the number of bytes allocated.
        std::size_t space = ROUND_UP_TO_MALLOCPAGE(block->blockSize + sizeof(BlockHead*));

        memDEBUG("Add memory to the free chain: " << SIZE(space));
        memDEBUG("Before getMemory(): mBase=" << ADDR(mBase) << ", mTop=" << ADDR(mTop) << ", available size=" << SIZE(mTop - mBase));

        ptr = getMemory(space);
        VERIFY_MALLOCPAGE_ALIGNED(ptr);                        // Verify MALLOCPAGE-alignment
        VERIFY_MALLOCPAGE_ALIGNED((uint64_t)(mBase - ptr));    // (mBase - ptr) should be equal to 'space'

        memDEBUG("After getMemory(): mBase=" << ADDR(mBase) << ", mTop=" << ADDR(mTop) << ", available size=" << SIZE(mTop - mBase));

        // We need to link the new memory onto this chain.
        // 1. Point the base of the new page(s) to the block header.
        // 2. Set block->ptr to point to this new space.
        char** base = (char**)ptr;    // donated memory pages base
        *base = (char*)block;           // memory page base points to owning block
        block->ptr = (char*)++base;     // block pointer points to our memory pages

        // Space now available for callers
        ptr = block->ptr;  

        // Build a free chain.
        // We need space in the page for the BlockHead pointer (8 bytes) and for each
        // object (size). We will preload the beginning 8 bytes of each object with
        // a chain pointer to the next object. The last object so preallocated will
        // be preloaded with zero.
        const char* chainEnd = block->ptr + space - sizeof(BlockHead*);
        const char* lastObj = chainEnd - block->blockSize;

        char* obj = nullptr;
        char* nextObj = block->ptr; // Beginning of the chain
        while(nextObj <= lastObj)
        {
            obj = nextObj;
            nextObj += block->blockSize;
            *(char**)obj = nextObj;
        }
        *(char**)obj = 0;  // Terminate free chain

        // victor test - TODO: Temporarily collecting statistics
        // Note: Collect statistics if mSharedData->blockStats is not nullptr.
        // Otherwise, if mSharedData->blockStats is not yet initialized, this allocation
        // is internal to the allocator's setup; do not include it in statistics collection.
        if(mSharedData->blockStats)
        {
            // Calculated wasted space, that is:
            // 1. Size of the free chain pointer at the beginning of the free chain.
            // 2. Remaining free chain space after the last object. 
            // 3. Internal Fragmentation, that occurs within an allocated memory block when
            // 'block->blockSize' (the allocated block) is larger then 'size' (the amount of memory requested). 
            std::size_t chainDust = sizeof(BlockHead*);         // Size of the free chain pointer
            chainDust += (chainEnd - obj - block->blockSize);   // Remaining free chain space after the last object

            // Internal Fragmentation
            if(block->blockSize > size)
                chainDust += (space - sizeof(BlockHead*)) / block->blockSize * (block->blockSize - size);

            __atomic_fetch_add(&mSharedData->chainDust, chainDust, __ATOMIC_RELAXED);
        }
    }

    // Point block->ptr to the next available object in a chain
    block->ptr = *(char**)ptr;
    *(char**)ptr = nullptr;  // Protect our chain

    // Verify that block->ptr is NOT MALLOCPAGE-aligned (is NOT on page boundary)
    VERIFY_NOT_MALLOCPAGE_ALIGNED(block->ptr);

    // Verify that ptr is NOT MALLOCPAGE-aligned (is NOT on page boundary)
    VERIFY_NOT_MALLOCPAGE_ALIGNED(ptr);

    memDEBUG("<--- Leave: size=" << SIZE(size) << ", ptr=" << ADDR(ptr) << ", block->ptr=" << (void*)block->ptr);

    // victor test - TODO: Temporarily collecting statistics
    // Note: Collect statistics if mSharedData->blockStats is not nullptr.
    // Otherwise, if mSharedData->blockStats is not yet initialized, this allocation
    // is internal to the allocator's setup; do not include it in statistics collection.
    if(mSharedData->blockStats)
    {
        // Update total row memory requested.
        // Note: The actual allocation will be aligned to the block size.
        __atomic_fetch_add(&mSharedData->blockStats[blockIndx].memRequested, size, __ATOMIC_RELAXED);
        __atomic_fetch_add(&mSharedData->blockStats[blockIndx].allocCount, 1, __ATOMIC_RELAXED);
    }
    // victor test end - TODO: Temporarily collecting statistics

    return ptr;  // Return user's block address
}

//
// Accept the memory returned by the user and re-chain it into our free chain.
// Detect cases where the user has just deleted this same object (double-free).
// Note: The process can only reuse its own memory. Memory returned
// by children will not be accessible by a parent and is generally lost.
//
inline void ShmAlloc::free(void* ptr)
{
    // If we are a child process making very first alloc/free call, 
    // then perform post-fork reset to make sure we are not using 
    // memory in the area already allocated by our mom.
    if(mOwnerPid != gCurrentPid)
    {
        // We must be a child process asking memory for a first time after fork
        if(!postForkReset())
            return;
        mOwnerPid = gCurrentPid;
    }

    if(!ptr)
        return;

    // Verify that ptr is NOT MALLOCPAGE-aligned (is NOT on page boundary)
    VERIFY_NOT_MALLOCPAGE_ALIGNED(ptr);

    // Allocated pages are MALLOCPAGE-aligned. To locate the owning block's address,
    // first use MALLOCPAGE_MASK to derive the page's base address. The owning block's
    // address is then stored within the first 8 bytes of that page.    
    void* page = (void*)(((uint64_t)ptr) & ~MALLOCPAGE_MASK);
    BlockHead* block = *(BlockHead**)page;

    memDEBUG("ptr=" << ptr << ", blockSize=" << block->blockSize);

    // Verify that page is MALLOCPAGE-aligned
    VERIFY_MALLOCPAGE_ALIGNED(page);    

    // Verify that block->ptr is NOT MALLOCPAGE-aligned (is NOT on page boundary)
    VERIFY_NOT_MALLOCPAGE_ALIGNED(block->ptr);

    // Once memory is re-chained, it becomes the first element in the block's free chain,
    // pointed to by 'block->ptr'. Immediate double-free attempts can be detected by checking
    // if the incoming 'ptr' is equal to 'block->ptr' (the current head of the free chain).
    if((char*)ptr == block->ptr)
    {
        // Immediate double delete detected.
        memERROR("Double delete of object at " << ptr << ".");
        crashWithCoreDump(0xbadbadbadde1e7e);   // dump core with special signature
    }

    // Re-chained the returned memory to the head of the block's free chain.
    *(char**)ptr = block->ptr;		
    block->ptr = (char*)ptr;

    // Verify that block->ptr is NOT MALLOCPAGE-aligned (is NOT on page boundary)
    VERIFY_NOT_MALLOCPAGE_ALIGNED(block->ptr);

    // The following block is Expiremental:
    if(mSysPageSize != 0)
    {
        // Releases physical resident memory back to the OS.
        // Only full pages contained within the range [ptr, ptr + size]
        // are released. Partial pages at the boundaries are preserved to
        // avoid corrupting adjacent allocations.

        // Convert to integer for bitwise math.
        uintptr_t startAddr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t endAddr = startAddr + block->blockSize;

        // Adjust startAddr by sizeof(char*), as that space stores
        // the address of the next block in the chain.
        // Align startAddr *UP* to the nearest page boundary
        // (We can't release the first page if ptr starts in the middle of it)
        startAddr += sizeof(char*);
        startAddr = (startAddr + mSysPageSize - 1) & ~(mSysPageSize - 1);

        // Align endAddr *DOWN* to the nearest page boundary
        // (We can't release the last page if the block ends in the middle of it)
        endAddr = endAddr & ~(mSysPageSize - 1);

        // If the range spans at least one full page, advise the kernel
        if(startAddr < endAddr)
        {
            size_t releaseSize = endAddr - startAddr;
            if(madvise(reinterpret_cast<void*>(startAddr), releaseSize, MADV_DONTNEED) != 0)
            {
                // This might not be fatal, but means the optimization won't happen
                std::string errmsg = strerror(errno);
                memERROR("Pid " << getpid() << ", allocator '" << mName << "' - "
                        "madvise(MADV_DONTNEED) failed for addr=" << reinterpret_cast<void*>(startAddr) <<
                        ", size=" << releaseSize << ": " << errmsg);
            }
        }
    } // Expiremental End
}

//
// We are out of memory on the free chain. Get memory either by recycling
// available from the large pages or by advancing to new memory base.
//
inline char* ShmAlloc::getMemory(std::size_t size)
{
    VERIFY_MALLOCPAGE_ALIGNED(size); // Verify MALLOCPAGE-alignment

    char* ptr = nullptr;

    // Do we have sufficient room on the current base?
    memDEBUG("mBase=" << (void*)mBase << ", mTop=" << (void*)mTop << ", size=" << SIZE(size));

    if(mBase + size <= mTop)
    {
        ptr = mBase;
        mBase += size;
        
        memDEBUG("The current base was extended: ptr=" << ADDR(ptr));
        VERIFY_MALLOCPAGE_ALIGNED(ptr); // Verify MALLOCPAGE-alignment
        return ptr;
    }

    // Check if a suitable large block is available on our free chain.
    // The 'size' parameter has already been adjusted: sizeof(BlockHead*) was added,
    // and the total was rounded up to the next MALLOCPAGE size before this routine was called.
    // Find the first block from the end that meets both criteria:
    // a) The block size is greater than or equal to MALLOCPAGE (meaning no multiple objects within one MALLOCPAGE).
    // b) The actual block memory is greater than or equal to that 'size'.
    BlockHead* blockZero = &mBlocks[mFirstLargeSlot];    // The first large block
    for(BlockHead* block = &mBlocks[mMaxSlots - 1]; block >= blockZero; block--)
    {
        if(!block->ptr)
            continue;   // The block has no memory

        if(block->blockSize < MALLOCPAGE || block->blockSize < size)
            continue;   // The block size is insufficient

        // If the current block's usable memory is insufficient, stop the search.
        // Subsequent blocks will be smaller, as the free chain is iterated in reverse (largest to smallest).
        std::size_t blockMemory = ROUND_UP_TO_MALLOCPAGE(block->blockSize + sizeof(BlockHead*));
        if(blockMemory < size)
            break;   // The actual block memory is insufficient

        // We found a block with a memory chunk large enough to satisfy our request
        memDEBUG("Found a large block: block->ptr=" << ADDR(block->ptr) << ", blockSize=" << SIZE(block->blockSize) 
                    << ", blockMemory=" << SIZE(blockMemory) << ", size=" << SIZE(size));

        ptr = block->ptr;
        block->ptr = *(char**)ptr;      // Remove from chain (point block's ptr to the next object in chain)
        *(char**)ptr = nullptr;         // Rrotect our chain
        ptr -= sizeof(BlockHead*);      // Reset to point to the begginning of the memory chunk  

        // If block's memory chunk larger then we need, then recycle the rest
        if(blockMemory > size)
        {
            memDEBUG("Recycle remaining " << SIZE(blockMemory - size) << " bytes at " << ADDR(ptr + size));
            recycleMemory(ptr + size, blockMemory - size);
        }
        break;
    }

    if(ptr)
    {
        memDEBUG("A suitable block was found on the free chain: ptr=" << ADDR(ptr));
        VERIFY_MALLOCPAGE_ALIGNED(ptr);    // Verify that ptr is MALLOCPAGE-aligned
        return ptr;
    }

    // No suitable large block is available on our free chain.
    // Advance the memory base if needed to accommodate the request.
    memDEBUG("Before advancing the base: mBase=" << ADDR(mBase) << ", mTop=" << ADDR(mTop) << ", size=" << SIZE(size));

    std::size_t delta = 0;

    // Note: Acquire lock to synchronize access to mSharedData
    {
        ShmLock lock(&mSharedData->lock, 180000, gCurrentPid);
        if(!lock)
        {
            memERROR("Pid " << getpid() << ", allocator '" << mName << "' - failed to get lock");
            audit(stdout, __func__);
            exit(EXIT_FAILURE);  // TODO: call crashWithCoreDump() instead?
        }

        memDEBUG("mSharedData->top=" << SIZE(mSharedData->top));

        std::size_t remaining = mStop - mSharedData->top;
        if(remaining < size)
        {
            // Not enough memory left
            memERROR("Pid " << getpid() << ", allocator '" << mName << "' - out of memory allocating " << size << " bytes");
            audit(stdout, __func__);
            exit(EXIT_FAILURE);  // TODO: call crashWithCoreDump() instead?
        }

        // Increment memory in EXTEND_MEM_SIZE
        delta = EXTEND_MEM_SIZE;
        while(delta < size)
            delta += EXTEND_MEM_SIZE;

        if(delta > remaining)
            delta = remaining;

        // If the new memory DOES NOT fit at the end of the old one (is not contiguous),
        // then update mBase.
        if(mSharedData->top != mTop)
        {
            // New memory is not contiguous. Recycle what we can between mBase and mTop
            if(mBase < mTop)
            {
                memDEBUG("New memory is not contiguous. Calling recycleMemory.");
                recycleMemory(mBase, mTop - mBase);   
            }

            // Update mBase
            mBase = mSharedData->top;
        }

        // Update mTop
        mSharedData->top += delta;
        mTop = mSharedData->top;
    }

    memDEBUG("After advancing the base: mBase=" << ADDR(mBase) << ", mTop=" << ADDR(mTop) << ", size=" << SIZE(size));

    // Advise the kernel TO dump the actually used portions.
    if(::madvise(mBase, delta, MADV_DODUMP) == -1)
    {
        // This might not be fatal, but means the optimization won't happen
        std::string errmsg = strerror(errno);
        memERROR("Pid " << getpid() << ", allocator '" << mName << "' - "
                 "madvise(MADV_DODUMP) failed for addr=" << mBase << ", size=" << delta << ": " << errmsg);
    }

    ptr = mBase;
    mBase += size;

    memDEBUG("The base was extended into a new memory segment: ptr=" << ADDR(ptr));
    VERIFY_MALLOCPAGE_ALIGNED(ptr); // Verify MALLOCPAGE-alignment
    return ptr;
}

//
// Release the main memory allocation block because we need to acquire more memory,
// and the new block isn't contiguous with the old one. We find the largest
// available block that can accommodate this memory and recycle it. This process
// repeats until either a suitable block is found for the recycled memory
// or an exact fit is achieved.
//
// We cannot utilize blocks smaller than a page; such blocks will be dispersed.
// Note that a block's length does not include the size of its header.
//
inline void ShmAlloc::recycleMemory(char* ptr, std::size_t size)
{
    std::size_t chainDust{0};       // 8-byte page headers and block ends.
    std::size_t dumpedMemory{0};    // Memory that is literally thrown away.

    while(size)
    {
        // Verify that ptr is MALLOCPAGE-aligned (ptr is on page boundary)
        // Verify that size is a multiple of MALLOCPAGE
        VERIFY_MALLOCPAGE_ALIGNED(ptr);
        VERIFY_MALLOCPAGE_ALIGNED(size);

        memDEBUG("Recycling " << SIZE(size) << " bytes at " << ADDR(ptr) 
                 << " (" << (size/MALLOCPAGE) << " page(s))");
        std::size_t available = (size <= sizeof(BlockHead*) ? 0 : size - sizeof(BlockHead*));

        // Discard memory blocks smaller than the minimum large block size 
        if(available < mBlocks[mFirstLargeSlot].blockSize)
        {
            dumpedMemory += size;   // Throw away remaining
            memDEBUG("Dumped " << SIZE(size) << " bytes at " << ADDR(ptr));
            break;
        }

        // Find the first block that is smaller than the amount of memory that
        // we have to recycle. Remember that we need room for the length header
        // as we are jamming in a whole page (at least). We will find somewhere
        // to put the memory since the test above determined that we will at least
        // fit in large block zero 'mBhLarge[0]'.
        BlockHead* block = &mBlocks[mMaxSlots-1];
        while(block->blockSize > (std::size_t)available)
            block--;

        memDEBUG("Giving memory at " << ADDR(ptr) << " to block " << SIZE(block->blockSize));

        // We need to link the new memory onto this chain.
        // To do that point the base of the new page(s) to the block header.
        // Next, set the next 8 bytes to the address currently in block->ptr.
        // Finally, set block->ptr to point to this new space.
        char** base = (char**)ptr;  // donated memory pages base
        *base++ = (char*)block;     // our memory page base points to its owning block
        *base = block->ptr;         // point base free chain to current block free chain
        block->ptr = (char*)base;   // block pointer now points to our memory pages

        // Verify that block->ptr is NOT MALLOCPAGE-aligned (is NOT on page boundary)
        VERIFY_NOT_MALLOCPAGE_ALIGNED(block->ptr);

        std::size_t recycled = block->blockSize + sizeof(BlockHead*);   // total size of recycled memory
        ptr += recycled;
        size -= recycled;

        // Now toss away page residue.
        // Calculate the number of bytes (padding) required to align 
        // the memory address 'ptr' to the next MALLOCPAGE-byte boundary
        std::size_t padding = PADDING_TO_MALLOCPAGE(ptr);

        //memDEBUG("ptr=" << ADDR(ptr) << ", padding=" << SIZE(padding));

        ptr += padding;
        size -= padding;

        memDEBUG("Remaining to recycle " << SIZE(size) << " bytes at " << ADDR(ptr) 
                 << " (" << (size/MALLOCPAGE) << " page(s))");

        // Update "chain dust" (8-byte page headers and block ends)
        chainDust += sizeof(BlockHead*) + padding;
    }

    // victor test - TODO: Temporarily collecting statistics
    // Note: Collect statistics if mSharedData->blockStats is not nullptr.
    // Otherwise, if mSharedData->blockStats is not yet initialized, this allocation
    // is internal to the allocator's setup; do not include it in statistics collection.
    if(mSharedData->blockStats)
    {
        if(chainDust > 0)
            __atomic_fetch_add(&mSharedData->chainDust, chainDust, __ATOMIC_RELAXED);
        if(dumpedMemory > 0)
            __atomic_fetch_add(&mSharedData->chainDust, dumpedMemory, __ATOMIC_RELAXED);
    }    
}

//
// Report memory usage and statistics to the specified output
//
inline void ShmAlloc::audit(FILE* out /*= stdout*/, const std::string& msg /*= ""*/)
{
    if(!msg.empty())
        fprintf(out, "[%s]:\n", msg.c_str());

    fprintf(out, "Allocator: '%s'\n", mName.c_str());

    // Collect per-slots totals
    std::size_t totalRequested = 0;
    std::size_t totalAllocated = 0;
    std::size_t totalAllocCount = 0;
    if(getTotals(totalRequested, totalAllocated, totalAllocCount, out))
    {
        fprintf(out, "Total: alloc %lu, memory %lu (requested %lu)\n", 
                totalAllocCount, totalAllocated, totalRequested);
    }

    fprintf(out, "            Capacity : %lu bytes (%p - %p)\n", ((uint64_t)mStop - (uint64_t)mStart), mStart, mStop);
    fprintf(out, "  Virtual Size (VSZ) : %lu bytes\n", getVSZ());
    fprintf(out, " Resident Size (RSS) : %lu bytes\n", getRSS());
    fprintf(out, "           Available : %lu bytes\n", (mTop - mBase));
    fprintf(out, "              Dumped : %lu bytes\n", mSharedData->dumpedMemory);
    fprintf(out, "          Chain dust : %lu bytes\n", mSharedData->chainDust);
    fflush(out);
}

inline bool ShmAlloc::getTotals(std::size_t& totalRequested, 
                                std::size_t& totalAllocated, 
                                std::size_t& totalAllocCount, 
                                FILE* out /*= nullptr*/) const 
{ 
    totalRequested = 0;
    totalAllocated = 0;
    totalAllocCount = 0;

    if(!mSharedData)
        return false;

    for(int i = 0; i < mMaxSlots; i++)
    {
        const SharedData::BlockStats& blockStats = mSharedData->blockStats[i];

        if(blockStats.allocCount > 0)
        {
            std::size_t blockSize = mBlocks[i].blockSize;
            std::size_t blockMemory = blockStats.allocCount * blockSize;
            totalRequested += blockStats.memRequested;
            totalAllocCount += blockStats.allocCount;
            totalAllocated += blockMemory;

            if(out)
                fprintf(out, "slot[%d]: size %lu, alloc %lu, memory %lu (requested %lu) bytes\n", 
                        i, blockSize, blockStats.allocCount, blockMemory, blockStats.memRequested);
        }
    }

    return true;
}

//
// Force program termination with a specific error value,
// intended to be readily visible during core dump analysis.
//
inline void ShmAlloc::crashWithCoreDump(uint64_t error) const
{
    // Intentionally write to an invalid memory address to crash 
    // the program and associate it with the specified error.
    uint64_t* coredump = (uint64_t*)1;
    *coredump = error;

    // Ensure termination as a fallback if the preceding crash 
    // attempt is optimized out or fails.
    abort();
}

//
// Child process must call this method before using allocator
//
inline bool ShmAlloc::postForkReset()
{
//    fprintf(stdout, ">>> %s: Child PID %d\n", __func__, getpid());

    // Dump all the currently available memory and set the fill pointer
    // to the top so that the next allocation must get a new segment.
    // The parallel children do this to avoid using memory in the area
    // already allocated by their mom.
    for(int i = mMaxSlots - 1; i >= 0; i--)
        mBlocks[i].ptr = nullptr;

    ShmLock lock(&mSharedData->lock, 180000, gCurrentPid);
    if(!lock)
    {
        memERROR( "Pid " << getpid() << " failed to get lock for allocator '" << getName() << "'.");
        return false;
    }

    mBase = mSharedData->top;
    mTop = mSharedData->top;
    return true;
}

// Check if the address fits within allocator range.
inline bool ShmAlloc::contains(const void* addr) const
{
    // Note: mStop is not included
    return (addr >= mStart && addr < mStop);
}

// Get the overall memory range covered by the allocator
inline std::size_t ShmAlloc::getVSZ() const
{
    return (mSharedData->top - mStart);
}

// Get how much memory is currently in use.
inline std::size_t ShmAlloc::getUsed() const
{
    // We consider all memory up to mSharedData->top as allocated, since
    // we cannot determine the exact amount consumed by other processes.
    // This means the reported total could be up to EXTEND_MEM_SIZE larger
    // than actual usage. Additionally, memory freed by other processes
    // is still considered taken because we cannot reuse it.
    return (mSharedData->top - mStart - getFree());
}

// Get how much memory is allocated but not in use.
inline std::size_t ShmAlloc::getFree() const
{
    // We only count memory available in this process's free chain.
    // We have no way to reuse any free chain memory from other processes.
    std::size_t total = mTop - mBase; 
    const BlockHead* bh = nullptr;

    for(int i = 0; i < mMaxSlots; i++)
    {
        if((bh = &mBlocks[i])->ptr)
        {
            for(const char* p = bh->ptr; p; p = *(char**)p)
            {
                total += bh->blockSize;
            }
        }
    }

    return total;
}

// Get the Resident Set Size (RSS) of the allocator
inline std::size_t ShmAlloc::getRSS() const
{
    // Get the system page size
    if(mSysPageSize == 0)
    {
        memERROR("Unable to calculate RSS; sysconf(_SC_PAGESIZE) returned an invalid value.");
        return 0;
    }

    // Calculate the number of pages in the region
    size_t length = mSharedData->top - mStart;
    size_t numPages = (length + mSysPageSize - 1) / mSysPageSize;

    // Prepare the residency status array
    std::unique_ptr<unsigned char[]> residencyStatusArray(new (std::nothrow) unsigned char[numPages]);
    if(!residencyStatusArray)
    {
        memERROR("Failed to allocate residency status buffer of " << numPages << " bytes");
        return 0;
    }

    // Call mincore()
    // The call fills the residencyStatusArray array:
    // 1 means the page is resident (in RAM)
    // 0 means the page is not resident (e.g., swapped out or never accessed)
    if(mincore(mStart, length, residencyStatusArray.get()) == -1)
    {
        std::string errmsg = strerror(errno);
        memERROR("mincore() failed: " << errmsg << ".");
        return 0;
    }

    // Count resident pages
    // The standard requires checking the Least Significant Bit (LSB).
    // The page is resident if the status byte has the LSB set (status & 0x1).
    long numResidentPages = 0;
    for(size_t i = 0; i < numPages; ++i)
    {
        if(residencyStatusArray[i] & 0x1)
            numResidentPages++;
    }

    // Calculate total resident bytes
    size_t rss = numResidentPages * mSysPageSize;
    memINFO("Total RAM (RSS pages) size " << rss);
    return rss;
}

//
// Static method to create a shared memory allocator.
//
// name  - Allocator name
// start - Mapping start address.
// stop  - Mapping stop address.
//
inline ShmAlloc* ShmAlloc::Create(const std::string& name, void* start, void* stop,
                                  bool verbose/*=false*/)
{
    memVERBOSE("Create new mapping"
            << ": name='" << name << "'"
            << ", start=" << start
            << ", stop=" << stop);

    // The Allocator memory address range must be properly alinged to support Allocator table
    if(!start || (uint64_t)start & ALLOC_BUCKET_ALIGNMENT_MASK)
    {
        memERROR("Allocator '" << name << "' start address " << start
                 << " is not " << SIZE(ALLOC_BUCKET_ALIGNMENT) << " aligned.");
        return nullptr;
    }

    if(!stop || (uint64_t)stop & ALLOC_BUCKET_ALIGNMENT_MASK)
    {
        memERROR("Allocator '" << name << "' stop address " << stop
                 << " is not " << SIZE(ALLOC_BUCKET_ALIGNMENT) << " aligned.");
        return nullptr;
    }

    // Check if start/stop range is free to use
    const Alloc* usedBy = FindAllocator(start, stop);
    if(usedBy)
    {
        // Allocator conflict detected
        memERROR("Allocator memory range conflict " << usedBy
                 << " allocator '" << name << "' and '" << usedBy->getName() << "'.");
        return nullptr;
    }

    // Allocator range must be at least MALLOCPAGE long
    std::size_t size = (std::uintptr_t)stop - (std::uintptr_t)start;
    if(size < MALLOCPAGE)
    {
        memERROR("Allocator memory address range is less than the minimum " << SIZE(MALLOCPAGE)
                 << ": name='" << name << "'"
                 << ", start=" << start
                 << ", stop=" << stop
                 << ", size=" << size);
        return nullptr;
    }

//  std::cout << ">>>> " << __func__ << ": start=" << std::hex << start << std::dec << std::endl;

    // Create shared memory.
    int mapFlags = MAP_FIXED | MAP_ANON | MAP_NORESERVE | MAP_SHARED;

    void* addr = mmap(start, size, PROT_READ | PROT_WRITE, mapFlags, -1, 0);
    if(addr == MAP_FAILED)
    {
        std::string errmsg = strerror(errno);
        memERROR("mmap() failed, start = " << start
                 << ", size = " << size << ": " << errmsg << ".");
        return nullptr;
    }

    if(addr != start)
    {
        // The returned address is not the requested address.
        if(munmap(addr, size) != 0)
        {
            std::string errmsg = strerror(errno);
            memERROR("munmap failed, addr = " << addr
                     << ", size = " << size << ": " << errmsg << ".");
        }

        memERROR("Error mapping memory. Allocator " << name
                 << " requested address " << start
                 << " but received address " << addr << ".");
        return nullptr;
    }

    // Advise the kernel NOT to dump this region by default.
    // This will reduces core dump size for unused portions.
    if(::madvise(addr, size, MADV_DONTDUMP) == -1)
    {
        // This might not be fatal, but means the optimization won't happen
        std::string errmsg = strerror(errno);
        memERROR("madvise(MADV_DONTDUMP) failed, addr = " << addr
                 << ", size = " << size << ": " << errmsg << ".");
    }

    // Success. We have new mapping.
    memVERBOSE("New mapping '" << name << "' for " << SIZE(size) << " bytes at " << ADDR(addr));

    // Create the local allocator.
    //int flags = ALIGN4BYTES;
    int flags = 0;

    ShmAlloc* al = new (std::nothrow) ShmAlloc(name, flags, start, stop);
    if(!al)
    {
        memERROR("Out of memory creating allocator " << name);
        if(munmap(addr, size) != 0)
        {
            std::string errmsg = strerror(errno);
            memERROR("munmap failed, addr = " << ADDR(addr)
                     << ", size = " << SIZE(size) << ": " << errmsg << ".");
        }
        return nullptr;
    }

    // Set the addresses from start to top as used.
    // TODO: error handling if failed
    if(!SetAllocator(al, true))
    {
        memERROR("Error adding allocator '" << name << "' to the global allocators table."
                 << " Either start address " << ADDR(start) << " or stop address " << ADDR(stop)
                 << " are not 16MB aligned.");
    }

    return al;
}

//
// Wrapper method to use uint64_t instead of void* for Allocator start/stop addresses
//
inline ShmAlloc* ShmAlloc::Create(const std::string& name, uint64_t start, uint64_t stop,
                                  bool verbose/*=false*/)
{
    // Call the Create() function that accepts void* start and stop arguments
    return Create(name, reinterpret_cast<void*>(start), reinterpret_cast<void*>(stop), verbose);
}

//
// Static method to create a shared memory allocator of desired size
//
// name - Allocator name
// size - Desired allocator address range
//
inline ShmAlloc* ShmAlloc::Create(const std::string& name, std::size_t size, 
                                  bool verbose/*=false*/)
{
    // Allocator range must be at least MALLOCPAGE long
    if(size < MALLOCPAGE)
    {
        memERROR("Allocator size is less than the minimum " << SIZE(MALLOCPAGE)
                 << ": name='" << name << "'"
                 << ", size=" << size);
        return nullptr;
    }

    // Allocated memory must be properly aligned to support Allocator table
    constexpr std::size_t ALIGNMENT = ALLOC_BUCKET_ALIGNMENT;

    // Calculate the required size for mmap.
    // We need enough space to guarantee a ALIGNMENT-aligned block of size.
    // Add (ALIGNMENT - 1) to ensure we can always find an aligned address
    // within the allocated region, even if the starting address is just
    // before an alignment boundary.
    std::size_t allocationSize = size + ALIGNMENT - 1;

    // Call mmap() to get a large enough region.
    int mapFlags = MAP_ANON | MAP_NORESERVE | MAP_SHARED;
    char* addr = (char*)mmap(nullptr, allocationSize, PROT_READ | PROT_WRITE, mapFlags, -1, 0);
    if(addr == MAP_FAILED) 
    {
        std::string errmsg = strerror(errno);
        memERROR("mmap() failed, size = " << SIZE(allocationSize) << ": " << errmsg << ".");
        return nullptr;
    }

    memVERBOSE("mmap() allocated base address: " << ADDR(addr));

    // Calculate the next ALIGNMENT-aligned address.
    char* alignedAddr = (char*)(((uint64_t)addr + ALIGNMENT - 1) & ~(ALIGNMENT - 1));

    memVERBOSE("Calculated aligned address: " << ADDR(alignedAddr));

    // Unmap the region *before* the aligned address.
    std::size_t prefixSize = alignedAddr - addr;
    if(prefixSize > 0) 
    {
        if(munmap(addr, prefixSize) == -1) 
        {
            std::string errmsg = strerror(errno);
            memERROR("munmap() failed to unmap prefix"
                     << ", addr = " << ADDR(addr)
                     << ", size = " << SIZE(prefixSize) << ": " << errmsg << "."); // TODO

            // Clean up the remaining part of the original mapping
            if(munmap(alignedAddr, allocationSize - prefixSize) == -1) 
            {
                std::string errmsg = strerror(errno);
                memERROR("munmap() failed to unmap remaining part after prefix munmap failure"
                        << ", addr=" << ADDR(alignedAddr)
                        << ", size=" << SIZE(allocationSize - prefixSize) << ": " << errmsg << ".");
            }

            // At this point, the process is in a very bad state regarding memory.
            // It might be best to abort or handle as a fatal application error.
            // crashWithCoreDump(....); // TODO: should we abort?
            return nullptr;
        }
        memVERBOSE("Unmapped " << prefixSize << " bytes before aligned address.");
    }

    // Unmap the region *after* the desired N bytes from the aligned address.
    char* suffixAddr = alignedAddr + size;
    std::size_t suffixSize = (addr + allocationSize) - suffixAddr;

    if(suffixSize > 0) 
    {
        if(munmap(suffixAddr, suffixSize) == -1) 
        {
            std::string errmsg = strerror(errno);
            memERROR("munmap() failed to unmap suffix"
                     << ", addr = " << ADDR(suffixAddr)
                     << ", size = " << SIZE(suffixSize) << ": " << errmsg << "."); // TODO

            // Clean up the remaining part of the original mapping
            if(munmap(alignedAddr, size) == -1) 
            {
                std::string errmsg = strerror(errno);
                memERROR("munmap() failed to unmap remaining part after suffix munmap failure"
                         << ", addr=" << ADDR(alignedAddr)
                         << ", size=" << SIZE(size) << ": " << errmsg << ".");
            }

            // At this point, the process is in a very bad state regarding memory.
            // It might be best to abort or handle as a fatal application error.
            // crashWithCoreDump(....); // TODO: should we abort?
            return nullptr;
        }
        memVERBOSE("Unmapped " << suffixSize << " bytes after desired size.");
    }

    // We're using mmap without MAP_FIXED (e.g., mmap(nullptr, ...)), so the OS should
    // find a clear range. Even so, we'll confirm that the allocated start/stop range is
    // truly available before proceeding.
    const Alloc* usedBy = FindAllocator(alignedAddr, alignedAddr + size);
    if(usedBy)
    {
        // Allocator conflict detected
        memERROR("Allocator memory range conflict " << usedBy
                 << " allocator '" << name << "' and '" << usedBy->getName() << "'.");

        if(munmap(alignedAddr, size) != 0)
        {
            std::string errmsg = strerror(errno);
            memERROR("munmap failed, addr = " << ADDR(alignedAddr)
                     << ", size = " << SIZE(size) << ": " << errmsg << ".");
        }
        return nullptr;
    }

    // Advise the kernel NOT to dump this region by default.
    // This will reduces core dump size for unused portions.
    if(::madvise(alignedAddr, size, MADV_DONTDUMP) == -1)
    {
        // This might not be fatal, but means the optimization won't happen
        std::string errmsg = strerror(errno);
        memERROR("madvise(MADV_DONTDUMP) failed, addr = " << addr
                 << ", size = " << size << ": " << errmsg << ".");
    }

    // Success. We have new mapping.
    memVERBOSE("New mapping '" << name << "' for " << SIZE(size) << " bytes at " << ADDR(alignedAddr));

    // Create the local allocator.
    //int flags = ALIGN4BYTES;
    int flags = 0;

    ShmAlloc* al = new (std::nothrow) ShmAlloc(name, flags, alignedAddr, alignedAddr + size);
    if(!al)
    {
        memERROR("Out of memory creating allocator " << name);
        if(munmap(alignedAddr, size) != 0)
        {
            std::string errmsg = strerror(errno);
            memERROR("munmap failed, addr = " << ADDR(alignedAddr)
                     << ", size = " << SIZE(size) << ": " << errmsg << ".");
        }
        return nullptr;
    }

    // Set the addresses from start to top as used.
    // TODO: error handling if failed
    if(!SetAllocator(al, true))
    {
        memERROR("Error adding allocator '" << name << "' to the global allocators table."
                 << " Either start address " << ADDR(alignedAddr) << " or stop address " << ADDR(alignedAddr + size)
                 << " are not 16MB aligned.");
    }

    return al;
}

} // namespace mem

//
// Overload global operator new to accept a std::unique_ptr<mem::ShmAlloc> reference
//
inline void* operator new(std::size_t size, const std::unique_ptr<mem::ShmAlloc>& al)
{
    return operator new(size, al.get());
}

inline void* operator new[](std::size_t size, const std::unique_ptr<mem::ShmAlloc>& al)
{
    return operator new[](size, al.get());
}

#endif // __SHMALLOC_HPP__

