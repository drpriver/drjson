//
// Copyright © 2022, David Priver
//
#ifndef HASH_FUNC_H
#define HASH_FUNC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline __attribute__((always_inline))
#else
#define force_inline
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
#else
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



#ifdef __ARM_ACLE
#include <arm_acle.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

static inline
uint32_t
hash_align1(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, (*(const packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, (*(const packed_uint16*)k).v);
    for(;len >= 1; k+=1, len-=1)
        h = __crc32cb(h, *(const uint8_t*)k);
    return h;
}
static inline
uint32_t
hash_align2(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, (*(const packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = __crc32ch(h, (*(const packed_uint16*)k).v);
    return h;
}
static inline
uint32_t
hash_align4(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = __crc32cw(h, (*(const packed_uint32*)k).v);
    return h;
}
static inline
uint32_t
hash_align8(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = __crc32cd(h, (*(const packed_uint64*)k).v);
    return h;
}

#elif defined(__x86_64__) && defined(__SSE4_2__)
#include <nmmintrin.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

static inline
uint32_t
hash_align1(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, (*(const packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, (*(const packed_uint16*)k).v);
    for(;len >= 1; k+=1, len-=1)
        h = _mm_crc32_u8(h, *(const uint8_t*)k);
    return h;
}
static inline
uint32_t
hash_align2(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, (*(const packed_uint32*)k).v);
    for(;len >= 2; k+=2, len-=2)
        h = _mm_crc32_u16(h, (*(const packed_uint16*)k).v);
    return h;
}
static inline
uint32_t
hash_align4(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const packed_uint64*)k).v);
    for(;len >= 4; k+=4, len-=4)
        h = _mm_crc32_u32(h, (*(const packed_uint32*)k).v);
    return h;
}
static inline
uint32_t
hash_align8(const void* key, size_t len){
    const unsigned char* k = key;
    uint32_t h = 0;
    for(;len >= 8; k+=8, len-=8)
        h = _mm_crc32_u64(h, (*(const packed_uint64*)k).v);
    return h;
}
#else // fall back to murmur hash

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif
// cut'n'paste from the wikipedia page on murmur hash
static inline
force_inline
uint32_t
murmur_32_scramble(uint32_t k) {
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
}
static inline
force_inline
uint32_t
hash_align1(const void* key_, size_t len){
    const uint8_t* key = key_;
    uint32_t seed = 4253307714;
	uint32_t h = seed;
    uint32_t k;
    /* Read in groups of 4. */
    for (size_t i = len >> 2; i; i--) {
        k = (*(const packed_uint32*)key).v;
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
static inline
force_inline
uint32_t
hash_align2(const void* key, size_t len){
    return hash_align1(key, len);
}
static inline
force_inline
uint32_t
hash_align4(const void* key, size_t len){
    return hash_align1(key, len);
}
static inline
force_inline
uint32_t
hash_align8(const void* key, size_t len){
    return hash_align1(key, len);
}

#endif

// This is faster than modulo if you just need to bring
// a into the range of [0, b) and a is already well
// distributed.
static inline
uint32_t fast_reduce32(uint32_t a, uint32_t b){
    return ((uint64_t)a * (uint64_t)b) >> 32;
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
