// A high-performance IPC nonblocking memory allocator.
// Copyright (c) 2026 @vkardon
// Project: https://github.com/vkardon/shmalloc
// SPDX-License-Identifier: AGPL-3.0-only
//
// alloc.hpp - Shared definitions and base allocator class
//
#pragma once

#include <cstdlib>      // std::malloc(), std::free
#include <limits>       // std::numeric_limits
#include <new>          // std::bad_array_new_length()
#include <cstdint>      // std::uintptr_t 
#include <cstddef>      // std::size_t

namespace mem {

//
// Alloc - Base class for allocators
//
class Alloc
{
public:
    Alloc() = default;
    virtual ~Alloc() = default;

    virtual void* alloc(std::size_t) = 0;
    virtual void free(void*) = 0;
    virtual const char* getName() const = 0;
    virtual void* getStart() const = 0;
    virtual void* getStop() const = 0;
};

//
// StlAlloc - STL wrapper for Alloc
//
template <typename T>
class StlAlloc
{
public:
    using value_type = T;

    // Custom constructor
    explicit StlAlloc(Alloc* alloc = nullptr) noexcept : mAlloc(alloc) {}

    // Converting copy constructor: Allows conversion from Alloc<U> to Alloc<T>
    template <typename U>
    StlAlloc(const StlAlloc<U>& other) noexcept : StlAlloc(other.mAlloc) {}

    // Two allocators are equal ONLY IF they manage the same underlying memory resource
    bool operator==(const StlAlloc& al) const noexcept { return (mAlloc == al.mAlloc); }
    bool operator!=(const StlAlloc& al) const noexcept { return (mAlloc != al.mAlloc); }

    // Allocate but don't initialize num elements of type T
    T* allocate(std::size_t num)
    {
        // Don't exceed maximum number of elements that can be allocated
        if(num == 0) 
            return nullptr;

        if(num > sMaxNumElements)
            throw std::bad_array_new_length();

        if(auto p = (mAlloc ? mAlloc->alloc(num * sizeof(T)) : std::malloc(num * sizeof(T))))
            return static_cast<T*>(p);

        throw std::bad_alloc();
    }

    // Deallocate storage "p" of deleted elements
    void deallocate(T* p, std::size_t /*num*/) noexcept { mAlloc ? mAlloc->free(p) : std::free(p); }

    // Initialize elements of allocated storage "p" with value "value"
    void construct(T* p, const T& value) { new (static_cast<void*>(p)) T(value); }

    // destroy elements of initialized storage "p"
    void destroy(T* p) { p->~T(); }

    // Guide std::allocator_traits on how to propagate the allocator when a container
    // is copied, moved, or assigned. We want propagation on assignment/move.
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    // Optional: for selecting the allocator when constructing a new container from an existing one
    // std::allocator_traits::select_on_container_copy_construction (by default just copy-constructs)

private:
    Alloc* mAlloc{nullptr};

    // Calculate this constant once per type T
    static constexpr std::size_t sMaxNumElements{ std::numeric_limits<std::size_t>::max() / sizeof(T) };

    // Grant access to all specializations of StlAlloc (to access mAlloc)
    template <typename V>
    friend class StlAlloc;
};

//
// Allocator table - to find allocator that corresponds to the given address
//
// We assume that every start and stop of allocator range is 16MB aligned.
// In this case the entire allocator range can be represented as a number of 16MB buckets.
// Then we can build a lookup table to translate any address to a corresponding bucket
// that represent a specific allocator. For this to work, the minimum allocation address
// needs to be 0x1000000 (that is 16MB = 24 bits).
// Since only 48 bits of 64-bit address are actually used, we can have 24 high-order bits
// to represent bucket index:
//
// 64 bit pointer: 0000 xxxx xxxx xxxx
//
//     | 0000    | xxxx xx                 | xx xxxx           |
//     | 16 bits | 24 bits                 | 24 bits           |
//     | Unused  | Bucket Index            | 16MB alignment    |
//     | Unused  | ALLOC_BUCKET_INDEX_BITS | ALLOC_BUCKET_BITS |
//
constexpr int ALLOC_BUCKET_BITS = 24;                                   // 2^24 (0x1000000) bytes (16777216 = 16 MB)
constexpr int ALLOC_BUCKET_INDEX_BITS  = 48 - ALLOC_BUCKET_BITS;        // 48-24=24 --> 2^24 = 16777216 buckets

// Allocated memory must be ALLOC_BUCKET_BITS-aligned to support Allocator table
constexpr std::uintptr_t ALLOC_BUCKET_ALIGNMENT = (1ULL << ALLOC_BUCKET_BITS);          // 0x1000000
constexpr std::uintptr_t ALLOC_BUCKET_ALIGNMENT_MASK = (ALLOC_BUCKET_ALIGNMENT - 1);    // 0x0FFFFFF

constexpr std::uintptr_t ALLOC_MIN_ADDR = (1ULL << ALLOC_BUCKET_BITS);  // 1<<24 = 2^24 = 0x1000000
constexpr std::uintptr_t ALLOC_MAX_ADDR = (1ULL << (ALLOC_BUCKET_INDEX_BITS + ALLOC_BUCKET_BITS)) - 1;
                                                                        // 1<<48-1 = 2^48-1 = 0x1000000000000-1;
//
// Note: The total address range available for allocation would be
// from 0x1000000 to 0x1000000000000-1, that is about 256TB
//

struct AllocBucket
{
    Alloc* alloc{nullptr};
};

// Max number of buckets
constexpr int ALLOC_BUCKETS = 1 << ALLOC_BUCKET_INDEX_BITS; // 2^ALLOC_BUCKET_INDEX_BITS = 2^24 = 16777216

inline AllocBucket AllocTable[ALLOC_BUCKETS]{}; // 16777216 buckets --> 128MB
inline AllocBucket NullBucket;

// TODO: To reduce AllocTable from 128MB to 64MB:
// Instead of storing the full allocator address externally, we can store
// it directly within its shared memory. Given that allocator's shared memory
// is always 16MB-aligned, we can discard the lowest 24 bits of this internal
// address. This allows us to represent the address using only its high-order
// bits (4 bytes instead of 8), which can be reconstructed to the full address
// when accessed.
//
// struct AllocBucket
// {
//     uint32_t shm{0};
// };
//
// inline Alloc* GetAllocator(void* p) 
// { 
//     if(uint32_t shm = GET_ALLOC_BUCKET(p)->shm; shm != 0)
//     {
//         char* ptr = (char*)((std::uintptr_t)shm << ALLOC_BUCKET_BITS);
//         return *(Alloc**)(ptr + sizeof(void*));
//     }
//     return nullptr;
//  }
//

// Get the allocator bucket (AllocTable entry) for the given address
#define GET_ALLOC_BUCKET(addr) \
        ((((std::uintptr_t)addr < mem::ALLOC_MIN_ADDR || (std::uintptr_t)addr > mem::ALLOC_MAX_ADDR)) ? \
          &mem::NullBucket : mem::AllocTable + ((std::uintptr_t)addr >> mem::ALLOC_BUCKET_BITS))

//
// Global method to get the allocator for the given address (if any)
//
inline Alloc* GetAllocator(void* p) { return GET_ALLOC_BUCKET(p)->alloc; }

//
// Set the table entries for the range of specified allocator
//
inline bool SetAllocator(Alloc* al, bool used)
{
    std::uintptr_t start = reinterpret_cast<std::uintptr_t>(al->getStart());
    std::uintptr_t stop = reinterpret_cast<std::uintptr_t>(al->getStop());

    // Allocator start/stop must be 16MB aligned (last ALLOC_BUCKET_BITS bits must be 0)
    if(!start || !stop || start >= stop || (start & ALLOC_BUCKET_ALIGNMENT_MASK) || (stop & ALLOC_BUCKET_ALIGNMENT_MASK))
        return false;

    AllocBucket* end = GET_ALLOC_BUCKET(stop);

    for(AllocBucket* bucket = GET_ALLOC_BUCKET(start); bucket < end; ++bucket)
    {
        bucket->alloc = (used ? al : nullptr);
    }

    return true;
}

//
//  Find allocator in start/stop range
//
inline const Alloc* FindAllocator(void* start, void* stop)
{
    AllocBucket* end = GET_ALLOC_BUCKET(stop);

    for(AllocBucket* bucket = GET_ALLOC_BUCKET(start); bucket < end; ++bucket)
    {
        if(bucket->alloc)
            return bucket->alloc;
    }

    return nullptr;
}

} // namespace mem

//
// Define custom new/delete operators to avoid mismatch when allocating
// from heap or from shared memory (via Allocator).
// Note: We must overload operator delete to properly deallocate the
// memory since it can be allocated from heap or via Allocator.
// This means that we also must overload operator new to make sure
// that memory allocation and deallocation match.
//
// Note: There is no placement delete expression. It is not possible to call any
// placement operator delete function using a delete expression. The placement
// delete functions are called from placement new expressions. In particular,
// they are called if the constructor of the object throws an exception. In such
// a circumstance, in order to ensure that the program does not incur a memory leak,
// the placement delete functions are called. A placement new expression first calls
// the placement operator new function, then calls the constructor of the object upon
// the raw storage returned from the allocator function. If the constructor throws
// an exception, it is necessary to deallocate that storage before propagating the
// exception back to the code that executed the placement new expression, and that
// is the purpose of the placement delete functions.
//
// The placement delete function that is called matches the placement new function
// that was invoked by the placement new expression.
//
// Note: The following placement new/delete does nothing and already defined in <new>
//inline void* operator new(std::size_t size, void* place)
//inline void* operator new[](std::size_t size, void* place)
//inline void  operator delete(void*, void*)
//inline void  operator delete[](void*, void*)

inline void* operator new(std::size_t size)
{
    return std::malloc(size);
}

inline void* operator new[](std::size_t size)
{
    return std::malloc(size);
}

inline void operator delete(void* p) noexcept
{
    if(p)
    {
        mem::Alloc* al = GET_ALLOC_BUCKET(p)->alloc;
        al ? al->free(p) : std::free(p);
    }
}

inline void operator delete(void* p, std::size_t /*size*/) noexcept
{
    operator delete(p);
}

inline void operator delete[](void* p) noexcept
{
    if(p)
    {
        mem::Alloc* al = GET_ALLOC_BUCKET(p)->alloc;
        al ? al->free(p) : std::free(p);
    }
}

inline void operator delete[](void* p, std::size_t /*size*/) noexcept
{
    operator delete[](p);
}

inline void* operator new(std::size_t size, mem::Alloc* al)
{
    return (al ? al->alloc(size) : std::malloc(size));
}

inline void* operator new[](std::size_t size, mem::Alloc* al)
{
    return (al ? al->alloc(size) : std::malloc(size));
}

inline void* operator new(std::size_t size, const std::unique_ptr<mem::Alloc>& al)
{
    return operator new(size, al.get());
}

inline void* operator new[](std::size_t size, const std::unique_ptr<mem::Alloc>& al)
{
    return operator new[](size, al.get());
}

inline void operator delete(void* p, mem::Alloc* al) noexcept
{
    if(p)
    {
        al ? al->free(p) : std::free(p);
    }
}

inline void operator delete[](void* p, mem::Alloc* al) noexcept
{
    if(p)
    {
        al ? al->free(p) : std::free(p);
    }
}
