#pragma once

#include <cstdint>
#include <cstddef>

#include <type_traits>

// #include <exception>

/////////////////////////////
// Memory Macros
/////////////////////////////


#ifdef FX_SCRIPT_USE_MEMPOOL

#include "FxMemPool.hpp"

#define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) FxMemPool::Alloc<ptrtype_>(size_)
#define FX_SCRIPT_ALLOC_NODE(nodetype_) FxMemPool::Alloc<nodetype_>(sizeof(nodetype_))
#define FX_SCRIPT_FREE(ptrtype_, ptr_) FxMemPool::Free<ptrtype_>(ptr_)

#else

// #define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) new ptrtype_[size_]
#define FX_SCRIPT_ALLOC_MEMORY(ptrtype_, size_) FxScriptAllocMemory<ptrtype_>(size_)
#define FX_SCRIPT_ALLOC_NODE(nodetype_) FxScriptAllocMemory<nodetype_>(sizeof(nodetype_))
#define FX_SCRIPT_FREE(ptrtype_, ptr_) FxScriptFreeMemory<ptrtype_>(ptr_)

#include <cstdlib>

#endif

template <typename T>
T* FxScriptAllocMemory(size_t size)
{
    T* ptr = reinterpret_cast<T*>(malloc(size));
    if constexpr (std::is_constructible_v<T>) {
        new (ptr) T;
    }

    return ptr;
}

template <typename T>
void FxScriptFreeMemory(T* ptr)
{
    // if constexpr (std::is_destructible_v<T>) {
    //     ptr->~T();
    // }

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


class FxUtil
{
public:
    static FILE* FileOpen(const char* path, const char* mode)
    {
        // TODO: readd fopen_s for Windows;
        return fopen(path, mode);
    }
};

/////////////////////////////
// Hashing Functions
/////////////////////////////

#define FX_HASH_FNV1A_SEED 0x811C9DC5
#define FX_HASH_FNV1A_PRIME 0x01000193

using FxHash = uint32;

/**
 * Hashes a string at compile time using FNV-1a.
 *
 * Source to algorithm: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
 */
inline constexpr FxHash FxHashStr(const char *str)
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

#define FX_BREAKPOINT __builtin_trap()

template <typename T, typename... Types>
void FxPanic(const char* const module, const char* fmt, T first, Types... items)
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
inline constexpr FxHash FxHashStr(const char *str, uint32 length)
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
