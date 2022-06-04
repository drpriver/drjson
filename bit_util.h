#ifndef BIT_UTIL_H
#define BIT_UTIL_H
#include <stdint.h>

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline __attribute__((always_inline))
#else
#define force_inline
#endif
#endif

// undefined if a = 0
static inline
force_inline
int
clz_32(uint32_t a) {
    #if defined(_MSC_VER) && !defined(__clang__)
        return _lzcnt_u32(a);
    #else
        return __builtin_clz(a);
    #endif
}

// undefined if a = 0
static inline
force_inline
int
clz_64(uint64_t a){
    #if defined(_MSC_VER) && !defined(__clang__)
        return _lzcnt_u64(a);
    #else
        return __builtin_clzll(a);
    #endif
}

// undefined if a = 0
static inline
force_inline
int
ctz_32(uint32_t a) {
    #if defined(_MSC_VER) && !defined(__clang__)
        return _tzcnt_u32(a);
    #else
        return __builtin_ctz(a);
    #endif
}

// undefined if a = 0
static inline
force_inline
int
ctz_64(uint64_t a) {
    #if defined(_MSC_VER) && !defined(__clang__)
        return _tzcnt_u64(a);
    #else
        return __builtin_ctzll(a);
    #endif
}

static inline
force_inline
int
popcount_32(uint32_t a){
    #if defined(_MSC_VER) && !defined(__clang__)
        return __popcnt(a);
    #else
        return __builtin_popcount(a);
    #endif
}

static inline
force_inline
int
popcount_64(uint32_t a){
    #if defined(_MSC_VER) && !defined(__clang__)
        return __popcnt64(a);
    #else
        return __builtin_popcountll(a);
    #endif
}



#endif
