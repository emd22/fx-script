#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// #include <exception>

#include <type_traits>

// #define FX_SCRIPT_USE_MEMPOOL 1

#ifdef FX_SCRIPT_USE_MEMPOOL

#include "FoxMemPool.hpp"

#define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) FoxMemPool::Alloc<ptrtype_>(size_)
#define FX_SCRIPT_ALLOC_NODE(nodetype_) FoxMemPool::Alloc<nodetype_>(sizeof(nodetype_))
#define FX_SCRIPT_FREE(ptrtype_, ptr_) FoxMemPool::Free<ptrtype_>(ptr_)

#else

// #define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) new ptrtype_[size_]
#define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) FoxAllocMemory<ptrtype_>(size_)
#define FX_SCRIPT_ALLOC_NODE(nodetype_) FoxAllocMemory<nodetype_>(sizeof(nodetype_))
#define FX_SCRIPT_FREE(ptrtype_, ptr_) FoxFreeMemory<ptrtype_>(ptr_)

#include <cstdlib>

#endif

template <typename T>
T* FoxAllocMemory(size_t size)
{
    T* ptr = reinterpret_cast<T*>(malloc(size));
    if constexpr (std::is_constructible_v<T>) {
        new (ptr) T;
    }

    return ptr;
}

template <typename T>
void FoxFreeMemory(T* ptr)
{
    if constexpr (std::is_destructible_v<T>) {
        ptr->~T();
    }

    free(ptr);
}

//////////////////
// Types
//////////////////


typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float float32;
typedef double float64;

#include <cstdio>

/////////////////////////////
// Utility Functions
/////////////////////////////


class FoxUtil
{
public:
    static FILE* FileOpen(const char* path, const char* mode)
    {
        // TODO: readd fopen_s for Windows;
        return std::fopen(path, mode);
    }
};

/////////////////////////////
// Hashing Functions
/////////////////////////////

#define FX_HASH_FNV1A_SEED 0x811C9DC5
#define FX_HASH_FNV1A_PRIME 0x01000193

using FoxHash = uint32;

/**
 * Hashes a string at compile time using FNV-1a.
 *
 * Source to algorithm: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
 */
inline constexpr FoxHash FoxHashStr(const char* str)
{
    uint32 hash = FX_HASH_FNV1A_SEED;

    unsigned char ch = 0;

    while ((ch = static_cast<unsigned char>(*(str++)))) {
        hash = (hash ^ ch) * FX_HASH_FNV1A_PRIME;
    }

    return hash;
}


#include <exception>
#include <iostream>

#ifdef _MSC_VER
#define FX_BREAKPOINT __debugbreak()
#else
#define FX_BREAKPOINT __builtin_trap()
#endif

template <typename T, typename... Types>
void FoxPanic(const char* const module, const char* fmt, T first, Types... items)
{
    // printf(fmt, items...);
    ((std::cout << items), ...);
    printf("\n");

    std::terminate();
}

/**
 * Hashes a string at compile time using FNV-1a.
 *
 * Source to algorithm: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
 */
inline constexpr FoxHash FoxHashStr(const char* str, uint32 length)
{
    uint32 hash = FX_HASH_FNV1A_SEED;

    unsigned char ch = 0;

    for (uint32 i = 0; i < length; i++) {
        ch = static_cast<unsigned char>(str[i]);

        if (ch == 0) {
            return hash;
        }

        hash = (hash ^ ch) * FX_HASH_FNV1A_PRIME;
    }

    return hash;
}
