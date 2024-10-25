//
// Copyright © 2022-2024, David Priver <david@davidpriver.com>
//
#ifndef HASH_FUNC_H
#define HASH_FUNC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef __clang__
#ifndef _Null_unspecified
#define _Null_unspecified
#endif
#endif

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define force_inline static inline __forceinline
#else
#define force_inline static inline
#endif
#endif

// dummy structs to allow unaligned loads.
// ubsan complains otherwise. This is sort of a grey
// area.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma pack(push)
#pragma pack(1)
typedef struct packed_uint64 packed_uint64;
struct packed_uint64 {
    uint64_t v;
};

typedef struct packed_uint32 packed_uint32;
struct packed_uint32 {
    uint32_t v;
};

typedef struct packed_uint16 packed_uint16;
struct packed_uint16 {
    uint16_t v;
};
#pragma pack(pop)
#elif defined(__clang__)
typedef struct packed_uint64 packed_uint64;
struct __attribute__((packed)) packed_uint64 {
    uint64_t v;
};

typedef struct packed_uint32 packed_uint32;
struct __attribute__((packed)) packed_uint32 {
    uint32_t v;
};

typedef struct packed_uint16 packed_uint16;
struct __attribute__((packed)) packed_uint16 {
    uint16_t v;
};
#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

#if defined(_MSC_VER) || defined(__clang__)
force_inline
uint64_t
read_unaligned8(const void* p){
    return ((const packed_uint64*)p)->v;
}
force_inline
uint32_t
read_unaligned4(const void* p){
    return ((const packed_uint32*)p)->v;
}
force_inline
uint16_t
read_unaligned2(const void* p){
    return ((const packed_uint16*)p)->v;
}
#else
// Gcc doesn't like the usage of packed structs for unaligned reads with -O2
// and above. Idk, it's an extension so it's whatever the compiler feels like
// I guess.
force_inline
uint64_t
read_unaligned8(const void* p){
    uint64_t v;
    __builtin_memcpy(&v, p, sizeof v);
    return v;
}
force_inline
uint32_t
read_unaligned4(const void* p){
    uint32_t v;
    __builtin_memcpy(&v, p, sizeof v);
    return v;
}
force_inline
uint16_t
read_unaligned2(const void* p){
    uint16_t v;
    __builtin_memcpy(&v, p, sizeof v);
    return v;
}
#endif
force_inline
uint8_t
read_unaligned1(const void* p){
    return *(const uint8_t*)p;
}



#if defined(__ARM_ACLE) && __ARM_FEATURE_CRC32
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#include <arm_acle.h>
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif


static inline
uint32_t
hash_align1(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, read_unaligned4(k));
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, read_unaligned2(k));
    for(;len >= 1; k+=1, len-=1)
        h = __crc32cb(h, read_unaligned1(k));
    return h;
}
static inline
uint32_t
hash_align2(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, read_unaligned4(k));
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, read_unaligned2(k));
    return h;
}
static inline
uint32_t
hash_align4(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, read_unaligned4(k));
    return h;
}
static inline
uint32_t
hash_align8(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, read_unaligned8(k));
    return h;
}

static inline
uint32_t
ascii_insensitive_hash_align1(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, 0x2020202020202020u|read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, 0x20202020u|read_unaligned4(k));
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, 0x2020u|read_unaligned2(k));
    for(;len >= 1; k+=1, len-=1)
        h = __crc32cb(h, 0x20u|read_unaligned1(k));
    return h;
}

#elif defined(__x86_64__) && defined(__SSE4_2__)
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#include <nmmintrin.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

static inline
uint32_t
hash_align1(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, read_unaligned4(k));
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, read_unaligned2(k));
    for(;len >= 1; k+=1, len-=1)
        h = _mm_crc32_u8(h, read_unaligned1(k));
    return h;
}
static inline
uint32_t
hash_align2(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, read_unaligned4(k));
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, read_unaligned2(k));
    return h;
}
static inline
uint32_t
hash_align4(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, read_unaligned4(k));
    return h;
}
static inline
uint32_t
hash_align8(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, read_unaligned8(k));
    return h;
}

static inline
uint32_t
ascii_insensitive_hash_align1(const void*_Null_unspecified key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, 0x2020202020202020u|read_unaligned8(k));
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, 0x20202020u|read_unaligned4(k));
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, 0x2020u|read_unaligned2(k));
    for(;len >= 1; k+=1, len-=1)
        h = _mm_crc32_u8(h, 0x20u|read_unaligned1(k));
    return h;
}
#else // fall back to murmur hash

// cut'n'paste from the wikipedia page on murmur hash
force_inline
uint32_t
murmur_32_scramble(uint32_t k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}
force_inline
uint32_t
hash_align1(const void*_Null_unspecified key_, size_t len){
    const uint8_t* key = key_;
    uint32_t seed = 4253307714;
	uint32_t h = seed;
    uint32_t k;
    /* Read in groups of 4. */
    for (size_t i = len >> 2; i; i--) {
        k = read_unaligned4(key);
        key += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }
    /* Read the rest. */
    k = 0;
    for (size_t i = len & 3; i; i--) {
        k <<= 8;
        k |= key[i - 1];
    }
    h ^= murmur_32_scramble(k);
    /* Finalize. */
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}
force_inline
uint32_t
hash_align2(const void*_Null_unspecified key, size_t len){
    return hash_align1(key, len);
}
force_inline
uint32_t
hash_align4(const void*_Null_unspecified key, size_t len){
    return hash_align1(key, len);
}
force_inline
uint32_t
hash_align8(const void*_Null_unspecified key, size_t len){
    return hash_align1(key, len);
}

force_inline
uint32_t
ascii_insensitive_hash_align1(const void*_Null_unspecified key_, size_t len){
    const uint8_t* key = key_;
    uint32_t seed = 4253307714;
	uint32_t h = seed;
    uint32_t k;
    /* Read in groups of 4. */
    for (size_t i = len >> 2; i; i--) {
        k = 0x20202020u|read_unaligned4(key);
        key += sizeof(uint32_t);
        h ^= murmur_32_scramble(k);
        h = (h << 13) | (h >> 19);
        h = h * 5 + 0xe6546b64;
    }
    /* Read the rest. */
    k = 0;
    for (size_t i = len & 3; i; i--) {
        k <<= 8;
        k |= 0x20u|key[i - 1];
    }
    h ^= murmur_32_scramble(k);
    /* Finalize. */
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define hash_alignany hash_align1
#else
#define hash_alignany(key, len) \
      (_Alignof(typeof(*key))&7) == 0? hash_align8(key, len) \
    : (_Alignof(typeof(*key))&3) == 0? hash_align4(key, len) \
    : (_Alignof(typeof(*key))&1) == 0? hash_align2(key, len) \
    :                        hash_align1(key, len)
#endif

static inline
uint32_t
fast_reduce32(uint32_t x, uint32_t y){
    return ((uint64_t)x * (uint64_t)y) >> 32;
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
