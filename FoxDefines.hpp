#pragma once


////////////////////////////
// Settings
////////////////////////////


#define FOX_ASM_OUTPUT_TO_STDOUT
#define FOX_ASM_OUTPUT_TO_FILE

#define FOX_LOG_OUTPUT_TO_STDOUT
#define FOX_LOG_OUTPUT_TO_FILE

#define FOX_LOG_ENABLE_COLORS

#define FX_MEMPOOL_USE_ATOMIC_LOCKING
#define FX_MEMPOOL_TRACK_STATISTICS
#define FX_MEMPOOL_WARN_SLOW_ALLOC
#define FX_MEMPOOL_NEXT_FIT

////////////////////////////////
// Platform/Compiler macros
////////////////////////////////

#ifdef FX_NO_SIMD
// FX_NO_SIMD defined
#elif defined(__ARM_NEON__)
#define FX_USE_NEON 1
#else
#define FX_NO_SIMD 1
#endif

#ifdef __APPLE__
#define FX_PLATFORM_MACOS 1
#elif _WIN64
#define FX_PLATFORM_WINDOWS 1
#else
#error "Unsupported platform"
#endif


#ifdef __clang__
#define FX_COMPILER_CLANG 1
#elif __GNUC__
#define FX_COMPILER_GCC 1
#elif _MSC_VER
#define FX_COMPILER_MSVC 1
#else
#error "Unsupported compiler"
#endif

#ifdef NDEBUG
#define FX_BUILD_RELEASE
#else
#define FX_BUILD_DEBUG
#endif


////////////////////////////////
// Global helper macros
////////////////////////////////

#ifdef _WIN64
#define FX_FORCE_INLINE __forceinline
#else
#define FX_FORCE_INLINE __attribute__((always_inline))
#endif
