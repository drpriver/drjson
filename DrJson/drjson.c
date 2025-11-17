//
// Copyright © 2022-2024, David Priver <david@davidpriver.com>
//
#ifndef DRJSON_C
#define DRJSON_C
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
typedef long long ssize_t;
#endif

#ifndef DRJSON_NO_STDIO
#include <stdio.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

#ifndef DRJ_HAVE_SIMD
    #if defined(__SSE2__) || (defined(_M_X64) && !defined(__clang__))
        #define DRJ_HAVE_SIMD 1
        #define DRJ_SIMD_WIDTH 16
        #include <emmintrin.h>
    #elif defined(__ARM_NEON) || defined(__aarch64__)
        #define DRJ_HAVE_SIMD 1
        #define DRJ_SIMD_WIDTH 16
        #include <arm_neon.h>
    #else
        #define DRJ_HAVE_SIMD 0
    #endif
#endif

#include "../compiler_warnings.h"
#ifndef DRJSON_API

#ifndef DRJSON_STATIC_LIB
#ifdef _WIN32
#define DRJSON_API extern __declspec(dllexport)
#else
#define DRJSON_API extern __attribute__((visibility("default")))
#endif
#else
#define DRJSON_API extern __attribute__((visibility("hidden")))
#endif
#endif

#include "drjson.h"
#include "drjson_itoa.h"
#include "hash_func.h"
#define PARSE_NUMBER_PARSE_FLOATS 1
#include "parse_numbers.h"
#define FPCONV_API static inline
#include "fpconv/src/fpconv.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
#endif
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

#if defined(__GNUC__) || defined(__clang__)
#define ALLOCATOR_SIZE(N) __attribute__((alloc_size(N)))
#define MALLOC_FUNC __attribute__((malloc))
#else
#define ALLOCATOR_SIZE(...)
#define MALLOC_FUNC
#endif

#if !defined(likely) && !defined(unlikely)
#if defined(__GNUC__) || defined(__clang__)
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif
#endif

#ifndef CASE_a_z
#define CASE_a_z 'a': case 'b': case 'c': case 'd': case 'e': case 'f': \
    case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': \
    case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': \
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z'
#endif

#ifndef CASE_A_Z
#define CASE_A_Z 'A': case 'B': case 'C': case 'D': case 'E': case 'F': \
    case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': \
    case 'N': case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': \
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z'
#endif

#ifndef CASE_A_F
#define CASE_A_F 'A': case 'B': case 'C': case 'D': case 'E': case 'F'
#endif

#ifndef CASE_a_f
#define CASE_a_f 'a': case 'b': case 'c': case 'd': case 'e': case 'f'
#endif

#ifndef CASE_0_9
#define CASE_0_9 '0': case '1': case '2': case '3': case '4': case '5': \
    case '6': case '7': case '8': case '9'
#endif
static
_Bool
drj_is_ro(DrJsonContext* ctx, DrJsonValue val);

static
DrJsonValue
drj_intern_object(DrJsonContext* ctx, DrJsonValue val, _Bool);

static
DrJsonValue
drj_intern_array(DrJsonContext* ctx, DrJsonValue val, _Bool);

typedef struct DrJsonObjectPair DrJsonObjectPair;
struct DrJsonObjectPair {
    DrJsonAtom atom;
    DrJsonValue value;
};

typedef struct DrJsonHashIndex DrJsonHashIndex;
struct DrJsonHashIndex {
    uint32_t index;
};

// #define DRJ_DEBUG


typedef struct DrJsonObject DrJsonObject;
struct DrJsonObject {
    void*_Nullable object_items;
    uint32_t count:31;
    uint32_t marked:1;
    uint32_t capacity:31;
    uint32_t read_only:1;
#ifdef DRJ_DEBUG
    _Bool freed:1;
#endif
};

typedef struct DrJsonArray DrJsonArray;
struct DrJsonArray {
    DrJsonValue*_Nullable array_items;
    uint32_t count:31;
    uint32_t marked:1;
    uint32_t capacity:31;
    uint32_t read_only:1;
#ifdef DRJ_DEBUG
    _Bool freed:1;
#endif
};

force_inline
uint32_t
drj_atom_get_hash(DrJsonAtom a){
    return (uint32_t)(a.bits >> 32u);
}

force_inline
uint32_t
drj_atom_get_idx(DrJsonAtom a){
    return (uint32_t)(a.bits & 0xffffffffu);
}

static inline
DrJsonAtom
drj_make_atom(uint32_t idx, uint32_t hash){
    uint64_t h = hash;
    uint64_t result = idx;
    result |= h << 32u;
    return (DrJsonAtom){result};
}

#define ATOM_MAX_LEN (UINT32_MAX/2)

typedef struct DrjAtomStr DrjAtomStr;
struct DrjAtomStr {
    uint32_t hash;
    uint32_t length:31;
    uint32_t allocated: 1;
    const char* pointer;
};

typedef struct DrjAtomTable DrjAtomTable;
struct DrjAtomTable {
    // layout:
    //      cap x [DrjAtomStr]
    //      2*cap x [uint32_t idx]
    void* data;
    uint32_t capacity; // in items
    uint32_t count; // in items
};

static inline
void
drj_atom_table_get_ptrs(void* p, size_t capacity, DrjAtomStr*_Nullable*_Nonnull atomstrs, uint32_t*_Nullable*_Nonnull idxes){
    *atomstrs = p;
    *idxes = (uint32_t*)(((char*)p) + capacity * sizeof(DrjAtomStr));
}

static inline
size_t
drj_atom_table_size_for(size_t cap){
    // XXX: alignment
    return cap * sizeof(DrjAtomStr)+ 2 * cap*sizeof(uint32_t);
}

static inline
DrjAtomStr
drj_get_atom_str(const DrjAtomTable* table, DrJsonAtom a){
    DrjAtomStr* strs = table->data;
    return strs[drj_atom_get_idx(a)];
}


force_inline
uint32_t
drj_hash_str(const char* key, size_t keylen){
    uint32_t h = hash_align1(key, keylen);
    if(!h) h = 1024;
    return h;
}

static inline
int
drj_grow_atom_table(DrjAtomTable* table, const DrJsonAllocator* allocator){
    size_t old_cap = table->capacity;
    uint32_t count = table->count;
    size_t new_cap = old_cap * 2;
    void* p = allocator->realloc(allocator->user_pointer, table->data, drj_atom_table_size_for(old_cap), drj_atom_table_size_for(new_cap));
    if(!p) return 1;
    DrjAtomStr* strs; uint32_t* idxes;
    drj_atom_table_get_ptrs(p, new_cap, &strs, &idxes);
    drj_memset(idxes, 0xff, 2*new_cap * sizeof *idxes);
    for(uint32_t i = 0; i < count; i++){
        const DrjAtomStr* s = &strs[i];
        uint32_t hash = s->hash;
        uint32_t idx = fast_reduce32(hash, (uint32_t)(2*new_cap));
        while(idxes[idx] != UINT32_MAX){
            idx++;
            if(idx >= 2*new_cap) idx = 0;
        }
        idxes[idx] = i;
    }
    table->data = p;
    table->capacity = (uint32_t)new_cap;
    return 0;
}

static inline
int
drj_atomize_str(DrjAtomTable* table, const DrJsonAllocator* allocator, const char* str, uint32_t len, _Bool copy, DrJsonAtom* outatom){
    if(unlikely(!len)) str = "";
    uint32_t hash = drj_hash_str(str, len);
    if(unlikely(!table->count)){
        assert(!table->capacity);
        assert(!table->data);
        enum {capacity = 32};
        void* p = allocator->alloc(allocator->user_pointer, drj_atom_table_size_for(capacity));
        if(!p) return 1;
        DrjAtomStr* strs; uint32_t* idxes;
        drj_atom_table_get_ptrs(p, capacity, &strs, &idxes);
        drj_memset(idxes, 0xff, 2*capacity * sizeof*idxes);
        table->data = p;
        table->capacity = capacity;
        uint32_t idx = fast_reduce32(hash, 2*capacity);
        _Bool copied = 0;
        if(copy && len){
            // XXX: use a string arena
            char* p = allocator->alloc(allocator->user_pointer, len);
            if(!p){
                allocator->free(allocator->user_pointer, p,drj_atom_table_size_for(capacity) );
                table->data = NULL;
                table->capacity = 0;
                return 1;
            }
            drj_memcpy(p, str, len);
            str = p;
            copied = 1;
        }
        strs[table->count] = (DrjAtomStr){
            .hash = hash,
            .length = len,
            .allocated = copied,
            .pointer = str,
        };
        *outatom = drj_make_atom(table->count, hash);
        idxes[idx] = table->count++;
        return 0;
    }
    if(unlikely(table->count >= table->capacity)){
        int err = drj_grow_atom_table(table, allocator);
        if(unlikely(err)) return err;
    }
    uint32_t capacity = table->capacity;
    uint32_t bounds = 2*capacity;
    uint32_t idx = fast_reduce32(hash, bounds);
    DrjAtomStr* strs; uint32_t* idxes;
    drj_atom_table_get_ptrs(table->data, capacity, &strs, &idxes);
    for(;;){
        uint32_t i = idxes[idx];
        if(i == UINT32_MAX){ // unset
            _Bool copied = 0;
            if(copy && len){
                // XXX: use a string arena
                char* p = allocator->alloc(allocator->user_pointer, len);
                if(!p) return 1;
                drj_memcpy(p, str, len);
                str = p;
                copied = 1;
            }
            strs[table->count] = (DrjAtomStr){
                .hash = hash,
                .length = len,
                .pointer = str,
                .allocated = copied,
            };
            *outatom = drj_make_atom(table->count, hash);
            idxes[idx] = table->count++;
            return 0;
        }
        DrjAtomStr a = strs[i];
        if(a.hash == hash && a.length == len && memcmp(str, a.pointer, len) == 0){
            *outatom = drj_make_atom(i, hash);
            return 0;
        }
        idx++;
        if(idx >= bounds) idx = 0;
    }
}

static inline
int
drj_get_atom_no_alloc(const DrjAtomTable* table, const char* str, uint32_t len,  DrJsonAtom* outatom){
    if(!table->count)
        return 1;
    uint32_t hash = drj_hash_str(str, len);
    uint32_t capacity = table->capacity;
    uint32_t idx = fast_reduce32(hash, 2*capacity);
    DrjAtomStr* strs; uint32_t* idxes;
    drj_atom_table_get_ptrs(table->data, capacity, &strs, &idxes);
    for(;;idx++){
        if(idx >= 2*capacity) idx = 0;
        uint32_t i = idxes[idx];
        if(i == UINT32_MAX) // unset
            return 1;
        DrjAtomStr a = strs[i];
        if(a.hash != hash)
            continue;
        if(a.length != len)
            continue;
        if(memcmp(str, a.pointer, len) == 0){
            *outatom = drj_make_atom(i, hash);
            return 0;
        }
    }
}

typedef struct DrjHashIdx DrjHashIdx;
struct DrjHashIdx {
    uint32_t hash;
    uint32_t idx;
};

struct DrJsonContext {
    DrJsonAllocator allocator;
    DrjAtomTable atoms;
    struct {
        DrJsonObject* data;
        size_t count;
        size_t capacity;
        size_t free_object;
    } objects;
    struct {
        DrjHashIdx* data;
        size_t count;
        size_t capacity;
    } interned_objects;
    struct {
        DrJsonArray* data;
        size_t count;
        size_t capacity;
        size_t free_array;
    } arrays;
    struct {
        DrjHashIdx* data;
        size_t count;
        size_t capacity;
    } interned_arrays;
    // Pre-atomized magic keys for allocation-free queries
    struct {
        DrJsonAtom length;
        DrJsonAtom keys;
        DrJsonAtom values;
        DrJsonAtom items;
    } magic_keys;
};

static inline
void*_Nullable
drj_alloc(const DrJsonContext* ctx, size_t size){
    return ctx->allocator.alloc(ctx->allocator.user_pointer, size);
}

static inline
void
drj_free(const DrJsonContext* ctx, const void* p, size_t size){
    ctx->allocator.free(ctx->allocator.user_pointer, p, size);
}

static inline
void*_Nullable
drj_realloc(const DrJsonContext* ctx, void*_Nullable p, size_t old_size, size_t new_size){
    if(!old_size || !p) return drj_alloc(ctx, new_size);
    if(!new_size){ drj_free(ctx, p, old_size); return NULL;}
    return ctx->allocator.realloc(ctx->allocator.user_pointer, p, old_size, new_size);
}

static inline
int
drj_get_str_and_len(const DrJsonContext* ctx, DrJsonValue v, const char*_Nullable*_Nonnull string, size_t* slen){
    if(v.kind != DRJSON_STRING)
        return 1;
    DrjAtomStr s = drj_get_atom_str(&ctx->atoms, v.atom);
    *string = s.pointer;
    *slen = s.length;
    return 0;
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_get_atom_str_and_length(const DrJsonContext* ctx, DrJsonAtom atom, const char*_Nullable*_Nonnull string, size_t* slen){
    DrjAtomStr s = drj_get_atom_str(&ctx->atoms, atom);
    *string = s.pointer;
    *slen = s.length;
    return 0;
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_get_str_and_len(const DrJsonContext* ctx, DrJsonValue v, const char*_Nullable*_Nonnull string, size_t* slen){
    return drj_get_str_and_len(ctx, v, string, slen);
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_get_atom_no_intern(const DrJsonContext* ctx, const char* str, size_t len, DrJsonAtom* outatom){
    if(len >= ATOM_MAX_LEN) return 1;
    if(!outatom) return 1;
    return drj_get_atom_no_alloc(&ctx->atoms, str, (uint32_t)len, outatom);
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_atomize(DrJsonContext* ctx, const char* str, size_t len, DrJsonAtom* outatom){
    if(len >= ATOM_MAX_LEN) return 1;
    if(!outatom) return 1;
    return drj_atomize_str(&ctx->atoms, &ctx->allocator, str, (uint32_t)len, 1, outatom);
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_atomize_no_copy(DrJsonContext* ctx, const char* str, size_t len, DrJsonAtom* outatom){
    if(len >= ATOM_MAX_LEN) return 1;
    if(!outatom) return 1;
    return drj_atomize_str(&ctx->atoms, &ctx->allocator, str, (uint32_t)len, 0, outatom);
}

DRJSON_API
DRJSON_WARN_UNUSED
DrJsonContext*_Nullable
drjson_create_ctx(DrJsonAllocator allocator){
    DrJsonContext* ctx = allocator.alloc(allocator.user_pointer, sizeof *ctx);
    if(!ctx) return NULL;
    drj_memset(ctx, 0, sizeof *ctx);
    ctx->allocator = allocator;

    // Pre-atomize magic keys for allocation-free queries
    // However, it's ok if they fail.
    int err;
    err = DRJSON_ATOMIZE(ctx, "length", &ctx->magic_keys.length);
    (void)err;
    err = DRJSON_ATOMIZE(ctx, "keys", &ctx->magic_keys.keys);
    (void)err;
    err = DRJSON_ATOMIZE(ctx, "values", &ctx->magic_keys.values);
    (void)err;
    err = DRJSON_ATOMIZE(ctx, "items", &ctx->magic_keys.items);
    (void)err;

    return ctx;
}

static inline
size_t
drjson_size_for_object_of_length(size_t len){
    return len * sizeof(DrJsonObjectPair) + 2*len*sizeof(DrJsonHashIndex);
}

force_inline
void
drj_get_obj_ptrs(void* p, size_t cap, DrJsonHashIndex*_Nullable*_Nonnull hi, DrJsonObjectPair*_Nullable*_Nonnull pa){
    *pa = p;
    *hi = (DrJsonHashIndex*)(((char*)p)+cap*sizeof(DrJsonObjectPair));
}

force_inline
DrJsonObjectPair*
drj_obj_get_pairs(void* p, size_t cap){
    (void)cap;
    return p;
}

force_inline
DrJsonHashIndex*
drj_obj_get_idxes(void* p, size_t cap){
    return (DrJsonHashIndex*)(((char*)p)+cap*sizeof(DrJsonObjectPair));
}




force_inline
int
drjson_object_set_item(DrJsonContext* ctx, DrJsonValue o, DrJsonAtom atom, DrJsonValue item);

static inline
ssize_t
alloc_obj(DrJsonContext* ctx){
    if(ctx->objects.free_object){
        ssize_t result = ctx->objects.free_object;
        DrJsonObject* p = &ctx->objects.data[result];
        ctx->objects.free_object = p->count;
        *p = (DrJsonObject){0};
        #ifdef DRJ_DEBUG
        fprintf(stderr, "Recycling object %p (%zu)\n", (void*)p, (size_t)result);
        #endif
        return result;
    }
    if(ctx->objects.capacity <= ctx->objects.count){
        size_t new_cap = ctx->objects.capacity? ctx->objects.capacity*2:8;
        DrJsonObject* p = ctx->allocator.realloc(ctx->allocator.user_pointer, ctx->objects.data, sizeof(DrJsonObject)*ctx->objects.capacity, sizeof(DrJsonObject)*new_cap);
        if(!p)
            return -1;
        ctx->objects.data = p;
        ctx->objects.capacity = new_cap;
        #ifdef DRJ_DEBUG
        fprintf(stderr, "reallocing object array\n");
        #endif
    }
    ssize_t result = ctx->objects.count++;
    DrJsonObject* odata = ctx->objects.data;
    odata[result] = (DrJsonObject){0};
    #ifdef DRJ_DEBUG
    fprintf(stderr, "allocated object %p (%zu)\n", (void*)&odata[result], (size_t)result);
    #endif
    return result;
}

static inline
ssize_t
alloc_array(DrJsonContext* ctx){
    if(ctx->arrays.free_array){
        ssize_t result = ctx->arrays.free_array;
        DrJsonArray* p = &ctx->arrays.data[result];
        ctx->arrays.free_array = p->count;
        *p = (DrJsonArray){0};
        #ifdef DRJ_DEBUG
        fprintf(stderr, "Recycling array %p (%zu)\n", (void*)p, (size_t)result);
        #endif
        return result;
    }
    if(ctx->arrays.capacity <= ctx->arrays.count){
        size_t new_cap = ctx->arrays.capacity? ctx->arrays.capacity*2:8;
        DrJsonArray* p = ctx->arrays.data? ctx->allocator.realloc(ctx->allocator.user_pointer, ctx->arrays.data, sizeof(DrJsonArray)*ctx->arrays.capacity, sizeof(DrJsonArray)*new_cap) : ctx->allocator.alloc(ctx->allocator.user_pointer, sizeof(DrJsonArray)*new_cap);
        if(!p)
            return -1;
        ctx->arrays.data = p;
        ctx->arrays.capacity = new_cap;
    }
    ssize_t result = ctx->arrays.count++;
    DrJsonArray* adata = ctx->arrays.data;
    adata[result] = (DrJsonArray){0};
    return result;
}

DRJSON_API
DrJsonValue
drjson_make_object(DrJsonContext* ctx){
    ssize_t idx = alloc_obj(ctx);
    if(idx < 0) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
    return (DrJsonValue){.kind=DRJSON_OBJECT, .object_idx=idx};
}

DRJSON_API
DrJsonValue
drjson_make_array(DrJsonContext* ctx){
    ssize_t idx = alloc_array(ctx);
    if(idx < 0) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
    return (DrJsonValue){.kind=DRJSON_ARRAY, .array_idx=idx};
}

MALLOC_FUNC
ALLOCATOR_SIZE(2)
static
void*_Null_unspecified
wrapped_malloc(void*_Null_unspecified _unused, size_t size){
    (void)_unused;
    return malloc(size);
}

ALLOCATOR_SIZE(4)
static
void*_Null_unspecified
wrapped_realloc(void*_Null_unspecified _unused, void*_Nullable data, size_t orig_size, size_t new_size){
    (void)_unused;
    (void)orig_size;
    if(!new_size){
        free(data);
        return NULL;
    }
    if(!data) return malloc(new_size);
    return realloc(data, new_size);
}

static
void
wrapped_free(void*_Null_unspecified _unused, const void*_Nullable data, size_t size){
    (void)_unused;
    (void)size;
    // PushDiagnostic();
    // SuppressCastQual();
    free((void*)data);
    // PopDiagnostic();
    return;
}

DRJSON_API
DrJsonAllocator
drjson_stdc_allocator(void){
    return (DrJsonAllocator){
        .alloc = wrapped_malloc,
        .realloc = wrapped_realloc,
        .free = wrapped_free,
    };
}

// NOTE: we consider commas and colons to be whitespace ;)
static inline
void
drj_skip_whitespace(DrJsonParseContext* ctx){
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    strip:
    for(;cursor != end; cursor++){
        if(*cursor <= 0x20) continue;
        switch(*cursor){
            case ' ': case '\r':
            case '\t': case '\n':
            case ',': case ':':
            case '=':
                continue;
            case '/':
                cursor++;
                goto comment;
            default:
                goto end;
        }
    }
    end:
    ctx->cursor = cursor;
    return;
    comment:
    if(cursor==end){
        goto end;
    }
    if(*cursor == '/'){
        cursor++;
        const char* p = memchr(cursor, '\n', end-cursor);
        if(!p) cursor = end;
        else cursor = p+1;
        goto strip;
    }
    else if(*cursor == '*'){
        cursor++;
        for(;;){
            const char* p = memchr(cursor, '*', end-cursor);
            if(p && p != end && p+1 != end && p[1] == '/'){
                cursor = p+2;
                goto strip;
            }
            if(p){
                cursor = p + 1;
            }
            else {
                cursor = end;
                goto end;
            }
        }
    }
    else{
        cursor--;
        goto end; // let parses take it as an invalid character
    }
}

force_inline
_Bool
drj_match(DrJsonParseContext* ctx, char c){
    if(ctx->cursor == ctx->end)
        return 0;
    if(*ctx->cursor != c)
        return 0;
    ctx->cursor++;
    return 1;
}

static inline
DrJsonValue
drj_make_atom_val(DrJsonParseContext* ctx, const char* str, size_t len){
    DrJsonAtom atom;
    int err = drj_atomize_str(&ctx->ctx->atoms, &ctx->ctx->allocator, str, (uint32_t)len, ctx->_copy_strings, &atom);
    if(err) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "failed to make atom");
    return (DrJsonValue){.kind = DRJSON_STRING, .atom=atom};
}

static inline
DrJsonValue
parse_string(DrJsonParseContext* ctx){
    drj_skip_whitespace(ctx);
    if(ctx->cursor == ctx->end)
        return drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "eof when beginning parsing string");
    const char* string_start;
    const char* string_end;
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    if(likely(drj_match(ctx, '"'))){
        cursor = ctx->cursor;
        string_start = cursor;
        for(;;){
            const char* close = memchr(cursor, '"', end-cursor);
            if(unlikely(!close)) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "No closing '\"' for a string");
            cursor = close+1;
            int nbackslashes = 0;
            int negidx = -1;
            while(close[negidx--] == '\\')
                nbackslashes++;
            if(nbackslashes & 1) continue;
            string_end = close;
            break;
        }
        ctx->cursor = cursor;
        return drj_make_atom_val(ctx, string_start, string_end-string_start);
    }
    else if(drj_match(ctx, '\'')){
        cursor = ctx->cursor;
        string_start = cursor;
        for(;;){
            const char* close = memchr(cursor, '\'', end-cursor);
            if(unlikely(!close)) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "No closing \"'\" for a string");
            cursor = close+1;
            int nbackslashes = 0;
            int negidx = -1;
            while(close[negidx--] == '\\')
                nbackslashes++;
            if(nbackslashes & 1) continue;
            string_end = close;
            break;
        }
        ctx->cursor = cursor;
        return drj_make_atom_val(ctx, string_start, string_end-string_start);
    }
    else {
        string_start = cursor;
        // allow bare identifiers
        for(;cursor != end; cursor++){
            switch(*cursor){
                default:
                    if(cursor == string_start) return drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "zero length when expecting a string");
                    else goto after2;
                case CASE_a_z:
                case CASE_A_Z:
                case CASE_0_9:
                case '_':
                case '-':
                case '.':
                case '/':
                case '+':
                case '*':
                    continue;
            }
        }
        after2:
        ctx->cursor = cursor;
        string_end = cursor;
        return drj_make_atom_val(ctx, string_start, string_end-string_start);
    }
}


static
DrJsonValue
drj_parse(DrJsonParseContext* ctx);

static inline
DrJsonValue
parse_object(DrJsonParseContext* ctx){
    if(unlikely(!drj_match(ctx, '{'))){
        return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Expected a '{' to begin an object");
    }
    DrJsonValue result = drjson_make_object(ctx->ctx);
    DrJsonValue error = {0};
    drj_skip_whitespace(ctx);
    while(!drj_match(ctx, '}')){
        if(unlikely(ctx->cursor == ctx->end)){
            error = drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "Eof before closing '}'");
            goto cleanup;
        }
        drj_skip_whitespace(ctx);
        DrJsonValue key = parse_string(ctx);
        if(unlikely(key.kind == DRJSON_ERROR)){
            error = key;
            goto cleanup;
        }
        DrJsonValue item = drj_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_object_set_item_atom(ctx->ctx, result, key.atom, item);
        if(unlikely(err)){
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate space for an item while setting member of an object");
            goto cleanup;
        }
        drj_skip_whitespace(ctx);
    }
    if(ctx->_read_only_objects)
        result = drj_intern_object(ctx->ctx, result, 1);
    return result;
    cleanup:
    return error;
}

static inline
DrJsonValue
parse_array(DrJsonParseContext* ctx){
    if(!drj_match(ctx, '[')) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Expected a '[' to begin an array");
    DrJsonValue result = drjson_make_array(ctx->ctx);
    DrJsonValue error = {0};
    drj_skip_whitespace(ctx);
    while(!drj_match(ctx, ']')){
        if(unlikely(ctx->cursor == ctx->end)){
            error = drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "Eof before closing ']'");
            goto cleanup;
        }
        DrJsonValue item = drj_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_array_push_item(ctx->ctx, result, item);
        if(unlikely(err)){
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to push an item onto an array");
            goto cleanup;
        }
        drj_skip_whitespace(ctx);
    }
    if(ctx->_read_only_objects)
        result = drj_intern_array(ctx->ctx, result, 1);
    return result;
    cleanup:
    return error;
}

static inline
DrJsonValue
parse_bool_null(DrJsonParseContext* ctx){
    ptrdiff_t length = ctx->end - ctx->cursor;
    if(length >= 4 && memcmp(ctx->cursor, "true", 4) == 0){
        ctx->cursor += 4;
        return drjson_make_bool(1);
    }
    if(length >= 5 && memcmp(ctx->cursor, "false", 5) == 0){
        ctx->cursor += 5;
        return drjson_make_bool(0);
    }
    if(length >= 4 && memcmp(ctx->cursor, "null", 4) == 0){
        ctx->cursor += 4;
        return drjson_make_null();
    }
    return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Invalid literal");
}
static inline
DrJsonValue
parse_number(DrJsonParseContext* ctx){
    const char* num_begin = ctx->cursor;
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    _Bool has_exponent = 0;
    _Bool has_decimal = 0;
    _Bool has_minus = 0;
    for(;cursor != end; cursor++){
        switch(*cursor){
            case 'e': case 'E':
                has_exponent = 1;
                continue;
            case '-':
                has_minus = 1;
                continue;
            case CASE_0_9:
            case '+':
                continue;
            case '.':
                has_decimal = 1;
                continue;
            default:
                goto after;
        }
    }
    after:;
    ptrdiff_t length = cursor - num_begin;
    if(!length) return drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "Zero length number");
    DrJsonValue result;
    if(has_exponent || has_decimal){
        DoubleResult pr = parse_double(num_begin, length);
        if(pr.errored){
            return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Failed to parse number");
        }
        result = drjson_make_number(pr.result);
    }
    else if(has_minus){
        Int64Result pr = parse_int64(num_begin, length);
        if(pr.errored){
            return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Failed to parse number");
        }
        result =  drjson_make_int(pr.result);
    }
    else {
        Uint64Result pr = parse_uint64(num_begin, length);
        if(pr.errored){
            return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Failed to parse number");
        }
        result =  drjson_make_uint(pr.result);
    }
    ctx->cursor = cursor;
    return result;
}

force_inline
unsigned
hexchar_to_value(char c){
    unsigned value = (uint8_t)c;
    value |= 0x20u;
    if(value > '9')
        return value - 'a' + 10u;
    return value - '0';
}

static inline
DrJsonValue
parse_color(DrJsonParseContext* ctx){
    const char* num_begin = ctx->cursor;
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    for(;cursor != end; cursor++){
        switch(*cursor){
            case CASE_0_9:
            case CASE_a_f:
            case CASE_A_F:
                continue;
            default:
                goto after;
        }
    }
    after:;
    ptrdiff_t length = cursor - num_begin;
    if(!length) return drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "0 length color");
    uint32_t value = 0x00000000;
    if(length == 3){
        value |= 0xff000000u;
        for(int i = 0; i < 3; i++){
            unsigned b = hexchar_to_value(num_begin[i]);
            b |= b << 4;
            value |= b << (i * 8);
        }
    }
    else if(length == 4){
        for(int i = 0; i < 4; i++){
            unsigned b = hexchar_to_value(num_begin[i]);
            b |= b << 4;
            value |= b << (i * 8);
        }
    }
    else if(length == 6){
        value |= 0xff000000u;
        for(int i = 0; i < 3; i++){
            unsigned hi = hexchar_to_value(num_begin[i*2]);
            unsigned lo = hexchar_to_value(num_begin[i*2+1]);
            value |= lo << (8*i);
            value |= hi << (8*i + 4);
        }

    }
    else if(length == 8){
        for(int i = 0; i < 4; i++){
            unsigned hi = hexchar_to_value(num_begin[i*2]);
            unsigned lo = hexchar_to_value(num_begin[i*2+1]);
            value |= lo << (8*i);
            value |= hi << (8*i + 4);
        }
    }
    else {
        return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "colors must be 3, 4, 6 or 8 numeric digits after the '#'");
    }
    ctx->cursor = cursor;
    return drjson_make_uint(value);
}
static inline
DrJsonValue
drj_parse_hex(DrJsonParseContext* ctx){
    const char* num_begin = ctx->cursor;
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    for(;cursor != end; cursor++){
        switch(*cursor){
            case CASE_0_9:
            case CASE_a_f:
            case CASE_A_F:
                continue;
            default:
                goto after;
        }
    }
    after:;
    ptrdiff_t length = cursor - num_begin;
    if(!length) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "0 length hex literal");
    if(length > 16) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Hex literal longer than 16 digits");
    uint64_t value = 0;
    for(ptrdiff_t i = 0; i < length; i++){
        value <<= 4;
        value |= hexchar_to_value(num_begin[i]);
    }
    ctx->cursor = cursor;
    return drjson_make_uint(value);
}

static
DrJsonValue
drjson_parse_braceless_object(DrJsonParseContext* ctx);


DRJSON_API
DrJsonValue
drjson_parse(DrJsonParseContext* ctx, unsigned flags){
    if(!(flags & DRJSON_PARSE_FLAG_NO_COPY_STRINGS))
        ctx->_copy_strings = 1;
    if(flags & DRJSON_PARSE_FLAG_INTERN_OBJECTS)
        ctx->_read_only_objects = 1;
    DrJsonValue result;
    if(flags & DRJSON_PARSE_FLAG_BRACELESS_OBJECT)
        result = drjson_parse_braceless_object(ctx);
    else
        result = drj_parse(ctx);

    // Check for trailing content if requested
    if((flags & DRJSON_PARSE_FLAG_ERROR_ON_TRAILING) && result.kind != DRJSON_ERROR){
        drj_skip_whitespace(ctx);
        if(ctx->cursor != ctx->end){
            return drjson_make_error(DRJSON_ERROR_TRAILING_CONTENT, "Unexpected content after JSON value");
        }
    }
    return result;
}

static
DrJsonValue
drj_parse(DrJsonParseContext* ctx){
    ctx->depth++;
    if(unlikely(ctx->depth > 100))
        return drjson_make_error(DRJSON_ERROR_TOO_DEEP, "Too many levels of nesting.");
    drj_skip_whitespace(ctx);
    DrJsonValue result;
    if(ctx->cursor == ctx->end)
        return drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "Eof before any values");
    switch(ctx->cursor[0]){
        case '{':
            result = parse_object(ctx);
            break;
        case '[':
            result = parse_array(ctx);
            break;
        case '\'':
        case '"':
            result = parse_string(ctx);
            break;
        case 't':
        case 'f':
        case 'n':
            result = parse_bool_null(ctx);
            if(result.kind == DRJSON_ERROR)
                result = parse_string(ctx);
            break;
        case '#':
            ctx->cursor++;
            result = parse_color(ctx);
            break;
        case '+':
        case '.': case '-':
        case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': case '8': case '9':
            result = parse_number(ctx);
            if(result.kind == DRJSON_ERROR)
                result = parse_string(ctx);
            break;
        case '0':
            if(ctx->cursor + 1 != ctx->end){
                if((ctx->cursor[1] | 0x20) == 'x'){
                    ctx->cursor += 2;
                    result = drj_parse_hex(ctx);
                    break;
                }
            }
            result = parse_number(ctx);
            if(result.kind == DRJSON_ERROR)
                result = parse_string(ctx);
            break;

        default:
            result = parse_string(ctx);
            if(result.kind != DRJSON_ERROR) break;
            result = drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Character is not a valid starting character for json");
            break;
    }
    ctx->depth--;
    return result;
}

DRJSON_API
DrJsonValue
drjson_parse_string(DrJsonContext* jctx, const char* text, size_t length, unsigned flags){
    DrJsonParseContext ctx = {
        .ctx = jctx,
        .begin = text,
        .cursor = text,
        .end = text+length,
        .depth = 0,
    };
    return drjson_parse(&ctx, flags);
}


static
DrJsonValue
drjson_parse_braceless_object(DrJsonParseContext* ctx){
    DrJsonValue result = drjson_make_object(ctx->ctx);
    DrJsonValue error = {0};
    ctx->depth++;
    drj_skip_whitespace(ctx);
    for(drj_skip_whitespace(ctx); ctx->cursor != ctx->end; drj_skip_whitespace(ctx)){
        DrJsonValue key = parse_string(ctx);
        if(unlikely(key.kind == DRJSON_ERROR)){
            error = key;
            goto cleanup;
        }
        DrJsonValue item = drj_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_object_set_item_atom(ctx->ctx, result, key.atom, item);
        if(unlikely(err)){
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate space for an item while setting member of an object");
            goto cleanup;
        }
    }
    ctx->depth--;
    return result;
    cleanup:
    return error;
}

DRJSON_API
int // 0 on success
drjson_array_push_item(const DrJsonContext* ctx, DrJsonValue a, DrJsonValue item){
    if(a.kind != DRJSON_ARRAY) return 1;
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(array->read_only) return 1;
    if(array->capacity < array->count+1u){
        size_t old_cap = array->capacity;
        enum {ARRAY_MAX = 0x1fffffff};
        size_t new_cap = old_cap?old_cap*2:4;
        if(new_cap > ARRAY_MAX) return 1;
        DrJsonValue* new_items = drj_realloc(ctx, array->array_items, old_cap*sizeof(*new_items), new_cap*sizeof(*new_items));
        if(!new_items) return 1;
        array->array_items = new_items;
        array->capacity = (uint32_t)new_cap;
    }
    array->array_items[array->count++] = item;
    return 0;
}

DRJSON_API
int // 0 on success
drjson_array_insert_item(const DrJsonContext* ctx, DrJsonValue a, size_t idx, DrJsonValue item){
    if(a.kind != DRJSON_ARRAY) return 1;
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(idx == array->count) return drjson_array_push_item(ctx, a, item);
    if(array->read_only) return 1;
    if(idx >= array->count) return 1;
    if(array->capacity < array->count+1u){
        size_t old_cap = array->capacity;
        enum {ARRAY_MAX = 0x1fffffff};
        size_t new_cap = old_cap?old_cap*2:4;
        if(new_cap > ARRAY_MAX) return 1;
        DrJsonValue* new_items = drj_realloc(ctx, array->array_items, old_cap*sizeof(*new_items), new_cap*sizeof(*new_items));
        if(!new_items) return 1;
        array->array_items = new_items;
        array->capacity = (uint32_t)new_cap;
    }
    size_t nmove = array->count - idx;
    drj_memmove(array->array_items+idx+1, array->array_items+idx, nmove * sizeof(*array->array_items));
    array->array_items[idx] = item;
    array->count++;
    return 0;
}

DRJSON_API
DrJsonValue
drjson_array_pop_item(const DrJsonContext* ctx, DrJsonValue a){
    if(a.kind != DRJSON_ARRAY) return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "Argument is not an array");
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(array->read_only)
        return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "Argument is read only");
    if(!array->count)
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "Array is empty");
    return array->array_items[--array->count];
}

DRJSON_API
int
drjson_clear(const DrJsonContext* ctx, DrJsonValue v){
    switch(v.kind){
        case DRJSON_ARRAY:{
            DrJsonArray* adata = ctx->arrays.data;
            DrJsonArray* array = &adata[v.array_idx];
            if(array->read_only) return 1;
            array->count = 0;
            return 0;
        }
        case DRJSON_OBJECT:{
            DrJsonObject* odata = ctx->objects.data;
            DrJsonObject* object = &odata[v.object_idx];
            if(object->read_only) return 1;
            if(object->capacity){
                DrJsonHashIndex* idxes; DrJsonObjectPair* pairs;
                drj_get_obj_ptrs(object->object_items, object->capacity, &idxes, &pairs);
                (void)pairs;
                drj_memset(idxes, 0xff, 2 * sizeof *idxes * object->capacity);
            }
            object->count = 0;
            return 0;
        }
        default:
            return 1;
    }
}

DRJSON_API
DrJsonValue
drjson_array_del_item(const DrJsonContext* ctx, DrJsonValue a, size_t idx){
    if(a.kind != DRJSON_ARRAY) return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "Argument is not an array");
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(array->read_only) return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "Argument is read only");
    if(!array->count)
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "Array is empty");
    if(idx >= array->count){
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "Index out of bounds.");
    }
    if(idx == array->count-1u)
        return drjson_array_pop_item(ctx, a);
    size_t nmove = array->count - idx-1;
    DrJsonValue result= array->array_items[idx];
    drj_memmove(array->array_items+idx, array->array_items+idx+1, nmove*sizeof(*array->array_items));
    array->count--;
    return result;
}

DRJSON_API
int
drjson_array_swap_items(const DrJsonContext* ctx, DrJsonValue a, size_t idx1, size_t idx2){
    if(a.kind != DRJSON_ARRAY) return 1;
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(array->read_only) return 1;
    if(idx1 >= array->count || idx2 >= array->count) return 1;
    if(idx1 == idx2) return 0; // Nothing to do

    DrJsonValue temp = array->array_items[idx1];
    array->array_items[idx1] = array->array_items[idx2];
    array->array_items[idx2] = temp;
    return 0;
}

DRJSON_API
int
drjson_array_move_item(const DrJsonContext* ctx, DrJsonValue a, size_t from_idx, size_t to_idx){
    if(a.kind != DRJSON_ARRAY) return 1;
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(array->read_only) return 1;
    if(from_idx >= array->count || to_idx >= array->count) return 1;
    if(from_idx == to_idx) return 0; // Nothing to do

    // Save the item to move
    DrJsonValue item = array->array_items[from_idx];

    // Shift items to close the gap
    if(from_idx < to_idx){
        // Moving forward: shift items left
        drj_memmove(array->array_items + from_idx,
                   array->array_items + from_idx + 1,
                   (to_idx - from_idx) * sizeof(*array->array_items));
    }
    else {
        // Moving backward: shift items right
        drj_memmove(array->array_items + to_idx + 1,
                   array->array_items + to_idx,
                   (from_idx - to_idx) * sizeof(*array->array_items));
    }

    // Place item at new position
    array->array_items[to_idx] = item;
    return 0;
}

DRJSON_API
int
drjson_object_move_item(const DrJsonContext* ctx, DrJsonValue o, size_t from_idx, size_t to_idx){
    if(o.kind != DRJSON_OBJECT) return 1;
    DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(object->read_only) return 1;
    if(from_idx >= object->count || to_idx >= object->count) return 1;
    if(from_idx == to_idx) return 0; // Nothing to do

    // Get pointers to object data
    DrJsonHashIndex* idxes;
    DrJsonObjectPair* pairs;
    drj_get_obj_ptrs(object->object_items, object->capacity, &idxes, &pairs);

    // Save the pair to move
    DrJsonObjectPair temp_pair = pairs[from_idx];

    // Shift items to close the gap and make space
    if(from_idx < to_idx){
        // Moving forward: shift pairs left
        drj_memmove(pairs + from_idx,
                   pairs + from_idx + 1,
                   (to_idx - from_idx) * sizeof(*pairs));
    }
    else {
        // Moving backward: shift pairs right
        drj_memmove(pairs + to_idx + 1,
                   pairs + to_idx,
                   (from_idx - to_idx) * sizeof(*pairs));
    }

    // Place pair at new position
    pairs[to_idx] = temp_pair;

    // Rebuild hash table since indices changed
    drj_memset(idxes, 0xff, 2 * sizeof(*idxes) * object->capacity);
    for(size_t i = 0; i < object->count; i++){
        const DrJsonObjectPair* p = &pairs[i];
        uint32_t hash = drj_atom_get_hash(p->atom);
        uint32_t idx = fast_reduce32(hash, (uint32_t)(2*object->capacity));
        while(idxes[idx].index != UINT32_MAX){
            idx++;
            if(idx >= 2*object->capacity) idx = 0;
        }
        idxes[idx].index = (uint32_t)i;
    }

    return 0;
}

force_inline
int
drjson_object_set_item(DrJsonContext* ctx, DrJsonValue o, DrJsonAtom atom, DrJsonValue item){
    if(o.kind != DRJSON_OBJECT) return 1;
    DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(object->read_only) return 1;
    enum {OBJECT_MAX = 0x1fffffff};
    if(unlikely(object->count >= object->capacity)){
        if(!object->capacity){
            size_t new_cap = 4;
            size_t size = drjson_size_for_object_of_length(new_cap);
            void* p = drj_alloc(ctx, size);
            if(!p) return 1;
            drj_memset(drj_obj_get_idxes(p, new_cap), 0xff, 2*new_cap*sizeof(DrJsonHashIndex));
            object->object_items = p;
            object->capacity = (uint32_t)new_cap;
        }
        else {
            size_t old_cap = object->capacity;
            size_t new_cap = old_cap * 2;
            if(new_cap > OBJECT_MAX) return 1;
            void* p = drj_realloc(ctx, object->object_items, drjson_size_for_object_of_length(old_cap), drjson_size_for_object_of_length(new_cap));
            if(!p) return 1;
            DrJsonHashIndex* idxes; DrJsonObjectPair* pairs;
            drj_get_obj_ptrs(p, new_cap, &idxes, &pairs);
            drj_memset(idxes, 0xff, 2*new_cap * sizeof *idxes);
            for(size_t i = 0; i < object->count; i++){
                const DrJsonObjectPair* p = &pairs[i];
                uint32_t hash = drj_atom_get_hash(p->atom);
                uint32_t idx = fast_reduce32(hash, (uint32_t)(2*new_cap));
                while(idxes[idx].index != UINT32_MAX){
                    idx++;
                    if(idx >= 2*new_cap) idx = 0;
                }
                idxes[idx].index = (uint32_t)i;
            }
            object->object_items = p;
            object->capacity = (uint32_t)new_cap;
        }
        #if 0
        if(object->capacity <= 32){
            DrJsonHashIndex* idxes = drj_obj_get_idxes(object->object_items, object->capacity);
            uint64_t seent = 0;
            for(size_t i = 0; i < object->capacity*2; i++){
                if(idxes[i].index == UINT32_MAX) continue;
                uint64_t mask = 1llu << idxes[i].index;
                if(mask & seent){
                    __builtin_debugtrap();
                }
                seent |= mask;
            }
        }
        #endif
    }

    #if 0
    if(object->capacity <= 32){
        DrJsonHashIndex* idxes = drj_obj_get_idxes(object->object_items, object->capacity);
        uint64_t seent = 0;
        for(size_t i = 0; i < object->capacity*2; i++){
            if(idxes[i].index == UINT32_MAX) continue;
            uint64_t mask = 1llu << idxes[i].index;
            if(mask & seent){
                __builtin_debugtrap();
            }
            seent |= mask;
        }
    }
    #endif
    uint32_t capacity = object->capacity;
    uint32_t hash = drj_atom_get_hash(atom);
    uint32_t idx = fast_reduce32(hash, 2*capacity);
    DrJsonHashIndex* idxes; DrJsonObjectPair* pairs;
    drj_get_obj_ptrs(object->object_items, object->capacity, &idxes, &pairs);
    for(;;){
        DrJsonHashIndex hi = idxes[idx];
        if(hi.index == UINT32_MAX){
            size_t pidx = object->count++;
            DrJsonObjectPair* o = &pairs[pidx];
            *o = (DrJsonObjectPair){
                .atom=atom,
                .value=item,
            };
            idxes[idx].index = (uint32_t)pidx;
            return 0;
        }
        DrJsonObjectPair* o = &pairs[hi.index];
        if(o->atom.bits == atom.bits){
            o->value = item;
            return 0;
        }
        idx++;
        if(idx >= 2*capacity)
            idx = 0;
    }
}

DRJSON_API
int // 0 on success
drjson_object_set_item_no_copy_key(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, DrJsonValue item){
    DrJsonAtom atom;
    int err = drj_atomize_str(&ctx->atoms, &ctx->allocator, key, (uint32_t)keylen, 1, &atom);
    if(err) return err;
    return drjson_object_set_item(ctx, object, atom, item);
}

DRJSON_API
int // 0 on success
drjson_object_set_item_copy_key(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, DrJsonValue item){
    DrJsonAtom atom;
    int err = drj_atomize_str(&ctx->atoms, &ctx->allocator, key, (uint32_t)keylen, 1, &atom);
    if(err) return err;
    return drjson_object_set_item(ctx, object, atom, item);
}

DRJSON_API
int // 0 on success
drjson_object_set_item_atom(DrJsonContext* ctx, DrJsonValue object, DrJsonAtom atom, DrJsonValue item){
    return drjson_object_set_item(ctx, object, atom, item);
}

DRJSON_API
int // 0 on success, 1 if key not found or error
drjson_object_delete_item_atom(DrJsonContext* ctx, DrJsonValue o, DrJsonAtom atom){
    if(o.kind != DRJSON_OBJECT) return 1;
    DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(object->read_only) return 1;
    if(object->count == 0) return 1;
    if(!object->capacity) return 1;

    uint32_t capacity = object->capacity;
    uint32_t hash = drj_atom_get_hash(atom);
    uint32_t hash_idx = fast_reduce32(hash, 2*capacity);
    DrJsonHashIndex* idxes;
    DrJsonObjectPair* pairs;
    drj_get_obj_ptrs(object->object_items, capacity, &idxes, &pairs);

    // Find the key in the hash table
    uint32_t found_hash_slot = UINT32_MAX;
    uint32_t found_pair_idx = UINT32_MAX;

    for(uint32_t idx = hash_idx;;){
        DrJsonHashIndex hi = idxes[idx];
        if(hi.index == UINT32_MAX) return 1; // Not found

        if(pairs[hi.index].atom.bits == atom.bits){
            found_hash_slot = idx;
            found_pair_idx = hi.index;
            break;
        }

        idx++;
        if(idx >= 2*capacity) idx = 0;
    }

    // Step 1: Shift pairs array to remove the deleted pair
    // This preserves insertion order
    if(found_pair_idx < object->count - 1)
        memmove(&pairs[found_pair_idx], &pairs[found_pair_idx + 1], (object->count - found_pair_idx - 1) * sizeof * pairs);
    object->count--;

    // Step 2: Decrement all hash indices > found_pair_idx
    // (they now point to shifted pairs)
    for(uint32_t i = 0; i < 2*capacity; i++){
        if(idxes[i].index != UINT32_MAX && idxes[i].index > found_pair_idx)
            idxes[i].index--;
    }

    // Step 3: Fix the probe chain using backward shift deletion
    // This maintains the linear probing invariant
    uint32_t i = found_hash_slot;
    for(;;){
        uint32_t j = i;

        // Look ahead in the probe sequence
        for(;;){
            j++;
            if(j >= 2*capacity) j = 0;

            // If we hit an empty slot, we're done
            if(idxes[j].index == UINT32_MAX){
                idxes[i].index = UINT32_MAX;
                return 0;
            }

            // Check if element at j can move to slot i
            // It can move if its ideal position is NOT in the range (i, j]
            uint32_t pair_hash = drj_atom_get_hash(pairs[idxes[j].index].atom);
            uint32_t k = fast_reduce32(pair_hash, 2*capacity);

            // Check if k is in range (i, j] (with wrapping)
            _Bool k_in_range;
            if(i < j)
                k_in_range = (k > i && k <= j);
            else // Wrapped range
                k_in_range = (k > i || k <= j);

            if(!k_in_range)
                break; // Element at j wants to be at k, which is not in (i,j]
                       // So it was probing through i, and we can move it back
        }

        // Move element from j to i
        idxes[i] = idxes[j];
        i = j;
    }
}

DRJSON_API
int // 0 on success, 1 if key not found or error
drjson_object_delete_item(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen){
    DrJsonAtom atom;
    int err = drj_get_atom_no_alloc(&ctx->atoms, key, (uint32_t)keylen, &atom);
    if(err) return 1; // Key not in atom table means it's not in the object
    return drjson_object_delete_item_atom(ctx, object, atom);
}

DRJSON_API
int // 0 on success, 1 if key not found or error
drjson_object_replace_key_atom(DrJsonContext* ctx, DrJsonValue o, DrJsonAtom old_key, DrJsonAtom new_key){
    if(o.kind != DRJSON_OBJECT) return 1;
    DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(object->read_only) return 1;
    if(object->count == 0) return 1;
    if(!object->capacity) return 1;

    uint32_t capacity = object->capacity;
    DrJsonHashIndex* idxes;
    DrJsonObjectPair* pairs;
    drj_get_obj_ptrs(object->object_items, capacity, &idxes, &pairs);

    // Find the old key in the pairs array
    uint32_t found_pair_idx = UINT32_MAX;
    for(uint32_t i = 0; i < object->count; i++){
        if(pairs[i].atom.bits == old_key.bits){
            found_pair_idx = i;
            break;
        }
    }

    if(found_pair_idx == UINT32_MAX) return 1; // Key not found

    // Check if new_key already exists (and is not the same as old_key)
    if(new_key.bits != old_key.bits){
        uint32_t new_key_hash = drj_atom_get_hash(new_key);
        uint32_t idx = fast_reduce32(new_key_hash, 2*capacity);
        for(;;){
            DrJsonHashIndex hi = idxes[idx];
            if(hi.index == UINT32_MAX) break; // New key doesn't exist, safe to proceed
            if(pairs[hi.index].atom.bits == new_key.bits){
                // New key already exists in the object
                return 1; // Would create duplicate
            }
            idx++;
            if(idx >= 2*capacity) idx = 0;
        }
    }

    // Replace the key in the pair
    pairs[found_pair_idx].atom = new_key;

    // Rebuild the hash table
    drj_memset(idxes, 0xff, 2*capacity * sizeof *idxes);
    for(uint32_t i = 0; i < object->count; i++){
        const DrJsonObjectPair* p = &pairs[i];
        uint32_t hash = drj_atom_get_hash(p->atom);
        uint32_t idx = fast_reduce32(hash, 2*capacity);
        while(idxes[idx].index != UINT32_MAX){
            idx++;
            if(idx >= 2*capacity) idx = 0;
        }
        idxes[idx].index = i;
    }

    return 0;
}

DRJSON_API
int // 0 on success, 1 if key already exists, index out of bounds, or error
drjson_object_insert_item_at_index(DrJsonContext* ctx, DrJsonValue o, DrJsonAtom key, DrJsonValue item, size_t index){
    if(o.kind != DRJSON_OBJECT) return 1;
    DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(object->read_only) return 1;

    // Check if index is valid (can be 0 to count inclusive, where count means append)
    if(index > object->count) return 1;

    enum {OBJECT_MAX = 0x1fffffff};

    // Check if key already exists
    if(object->count > 0 && object->capacity > 0){
        uint32_t capacity = object->capacity;
        uint32_t hash = drj_atom_get_hash(key);
        uint32_t hash_idx = fast_reduce32(hash, 2*capacity);
        DrJsonHashIndex* idxes;
        DrJsonObjectPair* pairs;
        drj_get_obj_ptrs(object->object_items, capacity, &idxes, &pairs);

        for(uint32_t idx = hash_idx;;){
            DrJsonHashIndex hi = idxes[idx];
            if(hi.index == UINT32_MAX) break; // Key doesn't exist, safe to proceed

            if(pairs[hi.index].atom.bits == key.bits){
                // Key already exists
                return 1;
            }

            idx++;
            if(idx >= 2*capacity) idx = 0;
        }
    }

    // Ensure capacity (same logic as drjson_object_set_item)
    if(unlikely(object->count >= object->capacity)){
        if(!object->capacity){
            size_t new_cap = 4;
            size_t size = drjson_size_for_object_of_length(new_cap);
            void* p = drj_alloc(ctx, size);
            if(!p) return 1;
            drj_memset(drj_obj_get_idxes(p, new_cap), 0xff, 2*new_cap*sizeof(DrJsonHashIndex));
            object->object_items = p;
            object->capacity = (uint32_t)new_cap;
        }
        else {
            size_t old_cap = object->capacity;
            size_t new_cap = old_cap * 2;
            if(new_cap > OBJECT_MAX) return 1;
            void* p = drj_realloc(ctx, object->object_items, drjson_size_for_object_of_length(old_cap), drjson_size_for_object_of_length(new_cap));
            if(!p) return 1;
            object->object_items = p;
            object->capacity = (uint32_t)new_cap;
            // Hash table will be rebuilt below
        }
    }

    uint32_t capacity = object->capacity;
    DrJsonHashIndex* idxes;
    DrJsonObjectPair* pairs;
    drj_get_obj_ptrs(object->object_items, capacity, &idxes, &pairs);

    // Shift pairs from index onwards to make room
    if(index < object->count){
        memmove(&pairs[index + 1], &pairs[index], (object->count - index) * sizeof(*pairs));
    }

    // Insert the new pair at the specified index
    pairs[index] = (DrJsonObjectPair){
        .atom = key,
        .value = item,
    };
    object->count++;

    // Rebuild the hash table to point to all pairs in their new positions
    drj_memset(idxes, 0xff, 2*capacity * sizeof(*idxes));
    for(uint32_t i = 0; i < object->count; i++){
        const DrJsonObjectPair* p = &pairs[i];
        uint32_t hash = drj_atom_get_hash(p->atom);
        uint32_t idx = fast_reduce32(hash, 2*capacity);
        while(idxes[idx].index != UINT32_MAX){
            idx++;
            if(idx >= 2*capacity) idx = 0;
        }
        idxes[idx].index = i;
    }

    return 0;
}

DRJSON_API
DrJsonValue
drjson_object_get_item_atom(const DrJsonContext* ctx, DrJsonValue o, DrJsonAtom atom){
    if(o.kind != DRJSON_OBJECT) return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "not an object");
    uint32_t hash = drj_atom_get_hash(atom);
    const DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(!object->capacity)
        return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "key is not valid for object");
    uint32_t capacity = object->capacity;
    uint32_t idx = fast_reduce32(hash, 2*capacity);
    DrJsonHashIndex* his; DrJsonObjectPair* pairs;
    drj_get_obj_ptrs(object->object_items, object->capacity, &his, &pairs);
    for(;;){
        DrJsonHashIndex hi = his[idx];
        if(hi.index == UINT32_MAX) return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "key is not valid for object");
        const DrJsonObjectPair* o = &pairs[hi.index];
        if(o->atom.bits == atom.bits)
            return o->value;
        idx++;
        if(idx >= 2*capacity)
            idx = 0;
    }
}

DRJSON_API
DrJsonValue
drjson_object_get_item(const DrJsonContext* ctx, DrJsonValue o, const char* key, size_t keylen){
    DrJsonAtom atom;
    int err = drj_get_atom_no_alloc(&ctx->atoms, key, (uint32_t)keylen, &atom);
    if(err) return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "key is not valid for object");
    return drjson_object_get_item_atom(ctx, o, atom);
}

DRJSON_API
DrJsonValue
drjson_checked_query(const DrJsonContext* ctx, DrJsonValue v, int type, const char* query, size_t query_length){
    DrJsonValue o = drjson_query(ctx, v, query, query_length);
    if(o.kind == DRJSON_ERROR) return o;
    if(o.kind != type)
        return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Wrong type");
    return o;
}


DRJSON_API
int
drjson_path_add_key(DrJsonPath* path, DrJsonAtom key){
    if(path->count >= DRJSON_PATH_MAX_DEPTH) return 1;
    path->segments[path->count++] = (DrJsonPathSegment){ .kind = DRJSON_PATH_KEY, .key = key };
    return 0;
}

DRJSON_API
int
drjson_path_add_index(DrJsonPath* path, int64_t index){
    if(path->count >= DRJSON_PATH_MAX_DEPTH) return 1;
    path->segments[path->count++] = (DrJsonPathSegment){ .kind = DRJSON_PATH_INDEX, .index = index };
    return 0;
}

DRJSON_API
int
drjson_path_parse(const DrJsonContext* ctx, const char* path_str, size_t path_len, DrJsonPath* path){
    const char* remainder = NULL;
    int err = drjson_path_parse_greedy(ctx, path_str, path_len, path, &remainder);
    if(err) return err;
    if(remainder != path_str + path_len) return 1; // Did not consume whole string
    return 0;
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_path_parse_greedy(const DrJsonContext* ctx, const char* path_str, size_t path_len, DrJsonPath* path, const char* _Nullable * _Nonnull remainder){
    size_t i = 0;
    size_t begin = 0;
    if(!path_str) return 1;
    if(!path) return 1;
    path->count = 0;
    if(path_len && path_str[0] == '$'){
        i = 1;
    }

    Ldispatch:
    if(i >= path_len) goto Ldone;

    switch(path_str[i]){
        case '.':
            i++;
            begin = i;
            if(i == path_len) return 1; // unexpected eof
            if(path_str[i] == '\"'){
                i++;
                begin=i;
                goto Lquoted_getitem;
            }
            goto Lgetitem;
        case '[':
            i++;
            begin = i;
            goto Lsubscript;
        case CASE_a_z:
        case CASE_A_Z:
        case '_':
            // Allow bare identifier at start of path
            if(i == 0 || (i == 1 && path_str[0] == '$')){
                begin = i;
                goto Lgetitem;
            }
            goto Ldone;
        default:
            if(i == 0 || (i == 1 && path_str[0] == '$')) return 1;
            goto Ldone;
    }

    Lgetitem:
    for(;i<path_len;i++){
        switch(path_str[i]){
            case '.':
            case '[':
                goto Ldo_getitem;
            case CASE_a_z:
            case CASE_A_Z:
            case CASE_0_9:
            case '/':
            case '_':
            case '-':
            case '+':
            case '*':
                continue;
            default:
                goto Ldo_getitem;
        }
    }
    Ldo_getitem:
    if(i == begin) return 1;
    {
        DrJsonAtom atom;
        // Use get_atom_no_intern for allocation-free queries
        int err = drjson_get_atom_no_intern(ctx, path_str + begin, i - begin, &atom);
        if(err){
            // Key not in atom table - won't be found in any object
            // Store sentinel value (bits = 0)
            atom = (DrJsonAtom){.bits = 0};
        }
        err = drjson_path_add_key(path, atom);
        if(err) return 1;
    }
    goto Ldispatch;

    Lsubscript:
    for(;i<path_len;i++){
        switch(path_str[i]){
            case '-':
            case CASE_0_9:
                continue;
            case ']':
                goto Ldo_subscript;
            default:
                return 1;
        }
    }
    return 1;
    Ldo_subscript:;
    Int64Result pr = parse_int64(path_str+begin, i-begin);
    if(pr.errored) return 1;
    int err = drjson_path_add_index(path, pr.result);
    if(err) return 1;
    i++;
    goto Ldispatch;

    Lquoted_getitem:
    for(;i<path_len;i++){
        if(path_str[i] == '\"'){
            size_t nbackslash = 0;
            for(size_t back = i-1; back != begin; back--){
                if(path_str[back] != '\\') break;
                nbackslash++;
            }
            if(nbackslash & 1) continue;
            {
                DrJsonAtom atom;
                // Use get_atom_no_intern for allocation-free queries
                int err = drjson_get_atom_no_intern(ctx, path_str + begin, i - begin, &atom);
                if(err){
                    // Key not in atom table - won't be found in any object
                    // Store sentinel value (bits = 0)
                    atom = (DrJsonAtom){.bits = 0};
                }
                err = drjson_path_add_key(path, atom);
                if(err) return 1;
            }
            i++;
            goto Ldispatch;
        }
    }
    return 1;

Ldone:
    *remainder = path_str + i;
    return 0;
}

DRJSON_API
DrJsonValue
drjson_query(const DrJsonContext* ctx, DrJsonValue v, const char* query, size_t length){
    DrJsonPath path;
    int err = drjson_path_parse(ctx, query, length, &path);
    if(err) return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Invalid query path");
    return drjson_evaluate_path(ctx, v, &path);
}

// Helper function to check if a key is a magic property and handle it
static
DrJsonValue
drjson_try_magic_key(const DrJsonContext* ctx, DrJsonValue current, DrJsonAtom key){
    // Check for magic properties using pre-atomized keys
    if(key.bits == ctx->magic_keys.length.bits){
        int64_t len = drjson_len(ctx, current);
        if(len < 0)
            return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "length on non-container");
        return drjson_make_int(len);
    }
    if(key.bits == ctx->magic_keys.keys.bits){
        return drjson_object_keys(current);
    }
    if(key.bits == ctx->magic_keys.values.bits){
        return drjson_object_values(current);
    }
    if(key.bits == ctx->magic_keys.items.bits){
        return drjson_object_items(current);
    }

    // Not a magic key
    return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "Key not found");
}

DRJSON_API
DrJsonValue
drjson_evaluate_path(const DrJsonContext* ctx, DrJsonValue v, const DrJsonPath* path){
    DrJsonValue current_value = v;
    for(size_t i = 0; i < path->count; i++){
        const DrJsonPathSegment* seg = &path->segments[i];
        if(seg->kind == DRJSON_PATH_KEY){
            // Check for sentinel (key not in atom table)
            if(seg->key.bits == 0){
                // Key doesn't exist in atom table, check if it's a magic key
                // (This shouldn't happen since we pre-atomize magic keys, but handle it anyway)
                return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "Key not found");
            }

            // Save the value before lookup
            DrJsonValue pre_lookup_value = current_value;

            // Try normal key lookup first
            current_value = drjson_object_get_item_atom(ctx, current_value, seg->key);

            // If that failed, try magic keys on the original value
            if(current_value.kind == DRJSON_ERROR){
                DrJsonValue magic_result = drjson_try_magic_key(ctx, pre_lookup_value, seg->key);
                // Only use magic result if it's not a "key not found" error
                if(magic_result.kind != DRJSON_ERROR || magic_result.error_code != DRJSON_ERROR_MISSING_KEY){
                    current_value = magic_result;
                }
            }
        } else { // DRJSON_PATH_INDEX
            current_value = drjson_get_by_index(ctx, current_value, seg->index);
        }
        if(current_value.kind == DRJSON_ERROR) return current_value;
    }
    return current_value;
}

DRJSON_API
int64_t
drjson_len(const DrJsonContext* ctx, DrJsonValue v){
    switch(v.kind){
        case DRJSON_ARRAY:
        case DRJSON_ARRAY_VIEW:{
            const DrJsonArray* adata = ctx->arrays.data;
            const DrJsonArray* array = &adata[v.array_idx];
            return array->count;
        }
        case DRJSON_OBJECT:
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_VALUES:{
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            return object->count;
        }
        case DRJSON_OBJECT_ITEMS:{
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            return 2*object->count;
        }
        case DRJSON_STRING:{
            DrjAtomStr s = drj_get_atom_str(&ctx->atoms, v.atom);
            return s.length;
        }
        default:
            return -1;
    }
}
DRJSON_API
DrJsonValue
drjson_get_by_index(const DrJsonContext* ctx, DrJsonValue v, int64_t idx){
    int64_t len = drjson_len(ctx, v);
    if(idx < 0) idx += len;
    size_t index  = idx < 0? (size_t)(idx + len) : (size_t)idx;
    switch(v.kind){
        case DRJSON_ARRAY:
        case DRJSON_ARRAY_VIEW:{
            const DrJsonArray* adata = ctx->arrays.data;
            const DrJsonArray* array = &adata[v.array_idx];
            if(array->count <= index)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            return array->array_items[index];
        }
        case DRJSON_OBJECT_KEYS:{
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            if(object->count <= index)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            size_t capacity = object->capacity;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, capacity);
            return drjson_atom_to_value(pairs[index].atom);
        }
        case DRJSON_OBJECT_VALUES:{
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            if(object->count <= index)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            size_t capacity = object->capacity;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, capacity);
            return pairs[index].value;
        }
        case DRJSON_OBJECT_ITEMS:{
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            size_t pidx = index/2;
            if(object->count <= pidx)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            size_t capacity = object->capacity;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, capacity);
            if(index & 1)
                return pairs[pidx].value;
            else
                return drjson_atom_to_value(pairs[pidx].atom);
        }
        default:
            return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "object does not support indexing by integer");
    }
}

DRJSON_API
int
drjson_array_set_by_index(const DrJsonContext* ctx, DrJsonValue a, int64_t idx, DrJsonValue value){
    if(a.kind != DRJSON_ARRAY) return 1;
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[a.array_idx];
    if(array->read_only) return 1;
    if(idx < 0) idx += array->count;
    if(idx < 0) return 1;
    if(idx >= array->count) return 1;
    array->array_items[idx] = value;
    return 0;
}

typedef struct DrJsonMemBuff DrJsonMemBuff;
struct DrJsonMemBuff {
    char* begin;
    char* end;
};

static
int mem_buff_write(void* p, const void* data, size_t len){
    DrJsonMemBuff* buff = p;
    if(buff->begin + len >= buff->end)
        return 1;
    drj_memcpy(buff->begin, data, len);
    buff->begin += len;
    return 0;
}

DRJSON_API
int
drjson_print_value_mem(const DrJsonContext* ctx, void* buff, size_t bufflen, DrJsonValue v, int indent, unsigned flags, size_t*_Nullable printed){
    DrJsonMemBuff membuff = {.begin = buff, .end = bufflen+(char*)buff};
    DrJsonTextWriter writer = {
        .up = &membuff,
        .write = mem_buff_write,
    };
    int result = drjson_print_value(ctx, &writer, v, indent, flags);
    if(!result && printed){
        *printed = membuff.begin - (char*)buff;
    }
    return result;
}

DRJSON_API
int
drjson_print_error_mem(void* buff, size_t bufflen, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v){
    DrJsonMemBuff membuff = {.begin = buff, .end = bufflen+(char*)buff};
    DrJsonTextWriter writer = {
        .up = &membuff,
        .write = mem_buff_write,
    };
    return drjson_print_error(&writer, filename, filename_len, line, column, v);
}

#ifndef DRJSON_NO_STDIO
static
int wrapped_fwrite(void* ud, const void* data, size_t length){
    return fwrite(data, 1, length, ud) != length;
}

DRJSON_API
#ifdef __clang__
__attribute__((__noinline__)) // clang is generating invalid code on windows without this
#endif
int
drjson_print_value_fp(const DrJsonContext* ctx, FILE* fp, DrJsonValue v, int indent, unsigned flags){
    DrJsonTextWriter writer = {
        .up = fp,
        .write = wrapped_fwrite,
    };
    return drjson_print_value(ctx, &writer, v, indent, flags);
}
DRJSON_API
#ifdef __clang__
__attribute__((__noinline__)) // probably ditto
#endif
int
drjson_print_error_fp(FILE* fp, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v){
    DrJsonTextWriter writer = {
        .up = fp,
        .write = wrapped_fwrite,
    };
    return drjson_print_error(&writer, filename, filename_len, line, column, v);
}
#endif

#ifndef DRJSON_NO_IO
#ifndef _WIN32
static
int
wrapped_write(void* restrict ud, const void* restrict data, size_t length){
    int fd = (int)(intptr_t)ud;
    return write(fd, data, length) != (ssize_t)length;
}
DRJSON_API
int
drjson_print_value_fd(const DrJsonContext* ctx, int fd, DrJsonValue v, int indent, unsigned flags){
    DrJsonTextWriter writer = {
        .up = (void*)(intptr_t)fd,
        .write = wrapped_write,
    };
    return drjson_print_value(ctx, &writer, v, indent, flags);
}
DRJSON_API
int
drjson_print_error_fd(int fd, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v){
    DrJsonTextWriter writer = {
        .up = (void*)(intptr_t)fd,
        .write = wrapped_write,
    };
    return drjson_print_error(&writer, filename, filename_len, line, column, v);
}
#else
static int
wrapped_write_file(void* restrict ud, const void* restrict data, size_t length){
    HANDLE hnd = ud;
    DWORD nwrit = 0;
    return WriteFile(hnd, data, length, &nwrit, NULL) != TRUE;
}
DRJSON_API
int
drjson_print_value_HANDLE(const DrJsonContext* ctx, void* hnd, DrJsonValue v, int indent, unsigned flags){
    DrJsonTextWriter writer = {
        .up = (void*)hnd,
        .write = wrapped_write_file,
    };
    return drjson_print_value(ctx, &writer, v, indent, flags);
}
DRJSON_API
int
drjson_print_error_HANDLE(void* hnd, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v){
    DrJsonTextWriter writer = {
        .up = (void*)hnd,
        .write = wrapped_write_file,
    };
    return drjson_print_error(&writer, filename, filename_len, line, column, v);
}
#endif
#endif

enum {DRJSON_BUFF_SIZE = 1024*512};
typedef struct DrJsonBuffered DrJsonBuffered;
struct DrJsonBuffered {
    const DrJsonTextWriter* writer;
    size_t cursor;
    int errored;
    char buff[DRJSON_BUFF_SIZE];
};


static inline
void
drjson_print_value_inner(const DrJsonContext* ctx, DrJsonBuffered* restrict buffer, DrJsonValue v);

static inline
void
drjson_pretty_print_value_inner(const DrJsonContext*_Nullable ctx, DrJsonBuffered* restrict buffer, DrJsonValue v, int indent);

static inline
void
drjson_buff_flush(DrJsonBuffered* restrict buffer){
    if(!buffer->errored)
        buffer->errored = buffer->writer->write(buffer->writer->up, buffer->buff, buffer->cursor);
    buffer->cursor = 0;
}

static inline
void
drjson_buff_ensure_n(DrJsonBuffered* restrict buffer, size_t length){
    if(buffer->cursor + length > DRJSON_BUFF_SIZE){
        drjson_buff_flush(buffer);
    }
}

static inline
void
drjson_buff_write(DrJsonBuffered* restrict buffer, const char* restrict data, size_t length){
    drjson_buff_ensure_n(buffer, length);
    if(length >= DRJSON_BUFF_SIZE){
        if(!buffer->errored)
            buffer->errored = buffer->writer->write(buffer->writer->up, data, length);
        return;
    }
    drj_memcpy(buffer->buff+buffer->cursor, data, length);
    buffer->cursor += length;
}

#define drjson_buff_write_lit(b, lit) drjson_buff_write(b, "" lit, sizeof(lit)-1)
static inline
void
drjson_buff_putc(DrJsonBuffered* restrict buffer, char c){
    drjson_buff_ensure_n(buffer, 1);
    buffer->buff[buffer->cursor++] = c;
}

DRJSON_API
int
drjson_print_value(const DrJsonContext* ctx, const DrJsonTextWriter* restrict writer, DrJsonValue v, int indent, unsigned flags){
    DrJsonBuffered buffer;
    buffer.cursor = 0;
    buffer.writer = writer;
    buffer.errored = 0;

    // Handle DRJSON_PRINT_BRACELESS for objects
    if((flags & DRJSON_PRINT_BRACELESS) && v.kind == DRJSON_OBJECT){
        const DrJsonObject* odata = ctx->objects.data;
        const DrJsonObject* object = &odata[v.object_idx];
        DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);

        if(flags & DRJSON_PRETTY_PRINT){
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(i != 0){
                    drjson_buff_putc(&buffer, ',');
                    drjson_buff_putc(&buffer, '\n');
                }
                for(int ind = 0; ind < indent; ind++)
                    drjson_buff_putc(&buffer, ' ');

                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(&buffer, '"');
                drjson_buff_write(&buffer, s.pointer, s.length);
                drjson_buff_putc(&buffer, '"');
                drjson_buff_putc(&buffer, ':');
                drjson_buff_putc(&buffer, ' ');
                drjson_pretty_print_value_inner(ctx, &buffer, o->value, indent);
            }
        }
        else {
            int newlined = 0;
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(&buffer, ',');
                newlined = 1;
                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(&buffer, '"');
                drjson_buff_write(&buffer, s.pointer, s.length);
                drjson_buff_putc(&buffer, '"');
                drjson_buff_putc(&buffer, ':');
                drjson_print_value_inner(ctx, &buffer, o->value);
            }
        }
    }
    else {
        if(flags & DRJSON_PRETTY_PRINT){
            for(int i = 0; i < indent; i++)
                drjson_buff_putc(&buffer, ' ');
            drjson_pretty_print_value_inner(ctx, &buffer, v, indent);
        }
        else
            drjson_print_value_inner(ctx, &buffer, v);
    }

    if(flags & DRJSON_APPEND_NEWLINE)
        drjson_buff_putc(&buffer, '\n');
    if(flags & DRJSON_APPEND_ZERO)
        drjson_buff_putc(&buffer, '\0');
    if(buffer.cursor)
        drjson_buff_flush(&buffer);
    return buffer.errored;
}

DRJSON_API
int
drjson_print_error(const DrJsonTextWriter* restrict writer, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v){
#if 0
    size_t line = 0;
    size_t column = 0;
    // just do it the slow way, whatever.
    for(const char* c = ctx->begin; c != ctx->cursor; c++){
        switch(*c){
            case '\n':
                line++;
                column = 0;
                break;
            default:
                column++;
                break;
        }
    }
#endif
    DrJsonBuffered buffer;
    buffer.cursor = 0;
    buffer.writer = writer;
    buffer.errored = 0;
    if(filename_len){
        drjson_buff_write(&buffer, filename, filename_len);
        drjson_buff_putc(&buffer, ':');
    }

    drjson_buff_ensure_n(&buffer, 20);
    buffer.cursor += drjson_uint64_to_ascii(buffer.buff+buffer.cursor, line+1);
    drjson_buff_putc(&buffer, ':');
    buffer.cursor += drjson_uint64_to_ascii(buffer.buff+buffer.cursor, column+1);
    drjson_buff_putc(&buffer, ':');
    drjson_buff_putc(&buffer, ' ');
    drjson_pretty_print_value_inner(NULL, &buffer, v, 0);
    drjson_buff_putc(&buffer, '\n');
    if(buffer.cursor)
        drjson_buff_flush(&buffer);
    return buffer.errored;
}
static inline
const char*
drjson_get_error_name(DrJsonValue);

static inline
size_t
drjson_get_error_name_length(DrJsonValue);

static inline
void
drjson_print_value_inner(const DrJsonContext* ctx, DrJsonBuffered* restrict buffer, DrJsonValue v){
    if(buffer->errored) return;
    switch(v.kind){
        case DRJSON_NUMBER:{
            drjson_buff_ensure_n(buffer, 24);
            int len = fpconv_dtoa(v.number, buffer->buff+buffer->cursor);
            buffer->cursor += len;
        }break;
        case DRJSON_INTEGER:
            drjson_buff_ensure_n(buffer, 20);
            buffer->cursor += drjson_int64_to_ascii(buffer->buff+buffer->cursor, v.integer);
            break;
        case DRJSON_UINTEGER:
            drjson_buff_ensure_n(buffer, 20);
            buffer->cursor += drjson_uint64_to_ascii(buffer->buff+buffer->cursor, v.uinteger);
            break;
        case DRJSON_STRING:{
            drjson_buff_putc(buffer, '"');
            const char* string = ""; size_t slen = 0;
            int err = drj_get_str_and_len(ctx, v, &string, &slen);
            (void)err;
            drjson_buff_write(buffer, string, slen);
            drjson_buff_putc(buffer, '"');
        }break;
        case DRJSON_ARRAY_VIEW:
        case DRJSON_ARRAY:{
            drjson_buff_putc(buffer, '[');
            const DrJsonArray* adata = ctx->arrays.data;
            const DrJsonArray* array = &adata[v.array_idx];
            for(size_t i = 0; i < array->count; i++){
                drjson_print_value_inner(ctx, buffer, array->array_items[i]);
                if(i != array->count-1u)
                    drjson_buff_putc(buffer, ',');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT:{
            drjson_buff_putc(buffer, '{');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, s.pointer, s.length);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ':');
                drjson_print_value_inner(ctx, buffer, o->value);
            }
            drjson_buff_putc(buffer, '}');
        }break;
        case DRJSON_OBJECT_KEYS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, s.pointer, s.length);
                drjson_buff_putc(buffer, '"');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_VALUES:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                drjson_print_value_inner(ctx, buffer, o->value);
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_ITEMS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, s.pointer, s.length);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ',');
                drjson_print_value_inner(ctx, buffer, o->value);
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_NULL:
            drjson_buff_write_lit(buffer, "null");
            break;
        case DRJSON_BOOL:
            if(v.boolean)
                drjson_buff_write_lit(buffer, "true");
            else
                drjson_buff_write_lit(buffer, "false");
            break;
        case DRJSON_ERROR:
            drjson_buff_write_lit(buffer, "Error: ");
            drjson_buff_write(buffer, drjson_get_error_name(v), drjson_get_error_name_length(v));
            drjson_buff_write_lit(buffer, "(Code ");
            drjson_buff_ensure_n(buffer, 20);
            buffer->cursor += drjson_int64_to_ascii(buffer->buff+buffer->cursor, v.error_code);
            drjson_buff_write_lit(buffer, "): ");
            drjson_buff_write(buffer, v.err_mess, v.err_len);
            break;
    }
}

static inline
void
drjson_pretty_print_value_inner(const DrJsonContext*_Nullable ctx, DrJsonBuffered* restrict buffer, DrJsonValue v, int indent){
    if(buffer->errored) return;
    switch(v.kind){
        case DRJSON_NUMBER:{
            drjson_buff_ensure_n(buffer, 24);
            int len = fpconv_dtoa(v.number, buffer->buff+buffer->cursor);
            buffer->cursor += len;
        }break;
        case DRJSON_INTEGER:
            drjson_buff_ensure_n(buffer, 20);
            buffer->cursor += drjson_int64_to_ascii(buffer->buff+buffer->cursor, v.integer);
            break;
        case DRJSON_UINTEGER:
            drjson_buff_ensure_n(buffer, 20);
            buffer->cursor += drjson_uint64_to_ascii(buffer->buff+buffer->cursor, v.uinteger);
            break;
        case DRJSON_STRING:{
            drjson_buff_putc(buffer, '"');
            const char* string = ""; size_t slen = 0;
            int err = drj_get_str_and_len(ctx, v, &string, &slen);
            (void)err;
            drjson_buff_write(buffer, string, slen);
            drjson_buff_putc(buffer, '"');
        }break;
        case DRJSON_ARRAY_VIEW:
        case DRJSON_ARRAY:{
            drjson_buff_putc(buffer, '[');
            const DrJsonArray* adata = ctx->arrays.data;
            const DrJsonArray* array = &adata[v.array_idx];
            int newlined = 0;
            if(array->count && !drjson_is_numeric(array->array_items[0])){
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
            }
            for(size_t i = 0; i < array->count; i++){
                if(newlined)
                    for(int i = 0; i < indent+2; i++)
                        drjson_buff_putc(buffer, ' ');
                drjson_pretty_print_value_inner(ctx, buffer, array->array_items[i], indent+2);
                if(i != array->count-1u)
                    drjson_buff_putc(buffer, ',');
                if(newlined)
                    drjson_buff_putc(buffer, '\n');
            }
            if(newlined){
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT:{
            drjson_buff_putc(buffer, '{');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');

                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, s.pointer, s.length);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ':');
                drjson_buff_putc(buffer, ' ');
                drjson_pretty_print_value_inner(ctx, buffer, o->value, indent+2);
            }
            if(newlined){
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, '}');
        }break;
        case DRJSON_OBJECT_KEYS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');

                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, s.pointer, s.length);
                drjson_buff_putc(buffer, '"');
            }
            if(newlined){
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_VALUES:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');
                drjson_pretty_print_value_inner(ctx, buffer, o->value, indent+2);
            }
            if(newlined){
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_ITEMS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* odata = ctx->objects.data;
            const DrJsonObject* object = &odata[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');

                DrjAtomStr s = drj_get_atom_str(&ctx->atoms, o->atom);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, s.pointer, s.length);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, ' ');
                drjson_pretty_print_value_inner(ctx, buffer, o->value, indent+2);
            }
            if(newlined){
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_NULL:
            drjson_buff_write_lit(buffer, "null");
            break;
        case DRJSON_BOOL:
            if(v.boolean)
                drjson_buff_write_lit(buffer, "true");
            else
                drjson_buff_write_lit(buffer, "false");
            break;
        case DRJSON_ERROR:
            drjson_buff_write_lit(buffer, "Error: ");
            drjson_buff_write(buffer, drjson_get_error_name(v), drjson_get_error_name_length(v));
            drjson_buff_write_lit(buffer, "(Code ");
            drjson_buff_ensure_n(buffer, 20);
            buffer->cursor += drjson_int64_to_ascii(buffer->buff+buffer->cursor, v.error_code);
            drjson_buff_write_lit(buffer, "): ");
            drjson_buff_write(buffer, v.err_mess, v.err_len);
            break;
    }
}

// return 1 - error
// return 2 - no need to escape
static
int
drjson_escape_string2(const DrJsonAllocator* restrict allocator, const char* restrict unescaped, size_t length, char *_Nullable restrict *_Nonnull restrict outstring, size_t* restrict outlength){
    if(!length) return 1;
    const char* const hex = "0123456789abcdef";
    size_t i = 0;
    for(; i < length; i++){
        switch(unescaped[i]){
            // 0x0 through 0x1f (0 through 31) have to all be
            // escaped with the 6 character sequence of \u00xx
            // Why on god's green earth did they force utf16 escapes?

            // gnu case ranges are nicer, but nonstandard
            // just spell them all out.
            case  0: case  1: case  2: case  3: case  4:
            // 8 is '\b', 9 is '\t'
            case  5: case  6: case  7:
            // 10 is '\n', 12 is '\f', 13 is '\r'
                     case 11:                   case 14:
            case 15: case 16: case 17: case 18: case 19:
            case 20: case 21: case 22: case 23: case 24:
            case 25: case 26: case 27: case 28: case 29:
            case 30: case 31:
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                goto needs_alloc;
            // Other characters are allowed through as is
            // (presumably utf-8).
            default:
                continue;
        }
    }
    return 2; // no escape
    needs_alloc:;

    // reserve 2 characters of output for every 2 characters of input.
    // This is enough space for all common json strings (the bytes that must
    // be escaped are rare).
    // We fix that up if we actually hit them.
    size_t allocated_size = length * 2;
    char* s = allocator->alloc(allocator->user_pointer, allocated_size);
    if(!s) return 1;
    if(i) drj_memcpy(s, unescaped, i);
    size_t cursor = i;
    for(; i < length; i++){
        switch(unescaped[i]){
            // 0x0 through 0x1f (0 through 31) have to all be
            // escaped with the 6 character sequence of \u00xx
            // Why on god's green earth did they force utf16 escapes?

            // gnu case ranges are nicer, but nonstandard
            // just spell them all out.
            case  0: case  1: case  2: case  3: case  4:
            // 8 is '\b', 9 is '\t'
            case  5: case  6: case  7:
            // 10 is '\n', 12 is '\f', 13 is '\r'
                     case 11:                   case 14:
            case 15: case 16: case 17: case 18: case 19:
            case 20: case 21: case 22: case 23: case 24:
            case 25: case 26: case 27: case 28: case 29:
            case 30: case 31:
                // These are rare, so only reserve more space when we actually hit them.
                {
                    size_t realloc_size = allocated_size+4;
                    char* news = allocator->realloc(allocator->user_pointer, s, allocated_size, realloc_size);
                    if(!news){
                        allocator->free(allocator->user_pointer, s, allocated_size);
                        return 1;
                    }
                    allocated_size = realloc_size;
                    s = news;
                }
                s[cursor++] = '\\';
                s[cursor++] = 'u';
                s[cursor++] = '0';
                s[cursor++] = '0';
                s[cursor++] = hex[(unescaped[i] & 0xf0)>>4];
                s[cursor++] = hex[(unescaped[i] & 0xf)];
                break;
            case '"':
                s[cursor++] = '\\';
                s[cursor++] = '"';
                break;
            case '\\':
                s[cursor++] = '\\';
                s[cursor++] = '\\';
                break;
            case '\b':
                s[cursor++] = '\\';
                s[cursor++] = 'b';
                break;
            case '\f':
                s[cursor++] = '\\';
                s[cursor++] = 'f';
                break;
            case '\n':
                s[cursor++] = '\\';
                s[cursor++] = 'n';
                break;
            case '\r':
                s[cursor++] = '\\';
                s[cursor++] = 'r';
                break;
            case '\t':
                s[cursor++] = '\\';
                s[cursor++] = 't';
                break;
            // Other characters are allowed through as is
            // (presumably utf-8).
            default:
                s[cursor++] = unescaped[i];
                break;
        }
    }
    // shrink to the size we actually used
    char* news = allocator->realloc(allocator->user_pointer, s, allocated_size, cursor);
    if(!news){
        allocator->free(allocator->user_pointer, s, allocated_size);
        return 1;
    }
    *outstring = news;
    *outlength = cursor;
    return 0;
}

DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_escape_string(DrJsonContext* ctx, const char* restrict unescaped, size_t length, DrJsonAtom* outatom){
    if(!outatom) return 1;
    if(length >= ATOM_MAX_LEN)
        return 1;
    if(!length){
        int err = drj_atomize_str(&ctx->atoms, &ctx->allocator, "", 0, 0, outatom);
        return err;
    }
    char* tmp;
    size_t tmp_length;
    int err = drjson_escape_string2(&ctx->allocator, unescaped, length, &tmp, &tmp_length);
    if(err == 1) return err;
    // String doesn't need to be escaped, make a copy in the atom table.
    const _Bool copy = 1;
    if(err == 2) return drj_atomize_str(&ctx->atoms, &ctx->allocator, unescaped, (uint32_t)length, copy, outatom);
    if(tmp_length >= ATOM_MAX_LEN)
        err = 1;
    else {
        // Always copy as we'd otherwise have to add a function that tells if it was interned or not.
        // so we know whether to free.
        // We could do that.
        err = drj_atomize_str(&ctx->atoms, &ctx->allocator, tmp, (uint32_t)tmp_length, copy, outatom);
    }
    drj_free(ctx, tmp, tmp_length);
    return err;
}

// Unescape a JSON string (convert \n, \t, \", \\, etc to actual characters)
// Returns: 0 on success, 1 on error
// outstring must have at least 'length' bytes available
// *outlength will be set to actual output length (always <= length)
DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_unescape_string(const char* restrict escaped, size_t length, char* restrict outstring, size_t* restrict outlength){
    if(!outlength) return 1;
    if(!length){
        *outlength = 0;
        return 0;
    }
    if(!escaped || !outstring) return 1;

    size_t in_pos = 0;
    size_t out_pos = 0;

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    // SIMD path: scan for backslashes in 16-byte chunks

    #if DRJ_HAVE_SIMD
    // Process 16 bytes at a time looking for backslashes
    while(in_pos + DRJ_SIMD_WIDTH <= length){
        #if defined(__SSE2__) || (defined(_M_X64) && !defined(__clang__))
            __m128i chunk = _mm_loadu_si128((const __m128i*)(escaped + in_pos));
            __m128i backslash = _mm_set1_epi8('\\');
            __m128i cmp = _mm_cmpeq_epi8(chunk, backslash);
            int mask = _mm_movemask_epi8(cmp);
        #elif defined(__ARM_NEON) || defined(__aarch64__)
            uint8x16_t chunk = vld1q_u8((const uint8_t*)(escaped + in_pos));
            uint8x16_t backslash = vdupq_n_u8('\\');
            uint8x16_t cmp = vceqq_u8(chunk, backslash);
            // Extract mask from comparison
            uint64_t mask_parts[2];
            vst1q_u8((uint8_t*)mask_parts, cmp);
            int mask = 0;
            for(int i = 0; i < 16; i++){
                if(((uint8_t*)mask_parts)[i])
                    mask |= (1 << i);
            }
        #endif

        if(mask == 0){
            // No backslashes in this chunk, copy directly
            #if defined(__SSE2__) || (defined(_M_X64) && !defined(__clang__))
                _mm_storeu_si128((__m128i*)(outstring + out_pos), chunk);
            #else
                vst1q_u8((uint8_t*)(outstring + out_pos), chunk);
            #endif
            in_pos += DRJ_SIMD_WIDTH;
            out_pos += DRJ_SIMD_WIDTH;
        }
        else {
            // Found backslash(es), handle byte by byte
            break;
        }
    }
    #endif
#else
    // SWAR path: scan for backslashes in 8-byte chunks using bit tricks
    #if defined(__LP64__) || defined(_WIN64)
    while(in_pos + 8 <= length){
        uint64_t chunk;
        drj_memcpy(&chunk, escaped + in_pos, 8);

        // Find backslashes using "has zero byte" trick
        // XOR with backslash pattern, then detect zeros
        uint64_t backslash_pattern = 0x5C5C5C5C5C5C5C5CULL; // '\\' repeated
        uint64_t xored = chunk ^ backslash_pattern;

        // Detect if any byte is zero: (x - 0x01...) & ~x & 0x80...
        uint64_t has_zero = (xored - 0x0101010101010101ULL) & ~xored & 0x8080808080808080ULL;

        if(has_zero == 0){
            // No backslashes, copy 8 bytes directly
            drj_memcpy(outstring + out_pos, &chunk, 8);
            in_pos += 8;
            out_pos += 8;
        }
        else {
            // Found backslash, handle byte by byte
            break;
        }
    }
    #endif
#endif

    // Scalar path: process remaining bytes or bytes with backslashes
    while(in_pos < length){
        char c = escaped[in_pos++];

        if(c != '\\'){
            outstring[out_pos++] = c;
            continue;
        }

        // Handle escape sequence
        if(in_pos >= length) return 1; // Trailing backslash

        char escape = escaped[in_pos++];
        switch(escape){
            case '"':  outstring[out_pos++] = '"'; break;
            case '\\': outstring[out_pos++] = '\\'; break;
            case '/':  outstring[out_pos++] = '/'; break;
            case 'b':  outstring[out_pos++] = '\b'; break;
            case 'f':  outstring[out_pos++] = '\f'; break;
            case 'n':  outstring[out_pos++] = '\n'; break;
            case 'r':  outstring[out_pos++] = '\r'; break;
            case 't':  outstring[out_pos++] = '\t'; break;
            case 'u': {
                // Unicode escape: \uXXXX
                if(in_pos + 4 > length) return 1;

                // Parse 4 hex digits
                uint32_t codepoint = 0;
                for(int i = 0; i < 4; i++){
                    char hex = escaped[in_pos++];
                    uint32_t digit;
                    if(hex >= '0' && hex <= '9')
                        digit = hex - '0';
                    else if(hex >= 'a' && hex <= 'f')
                        digit = hex - 'a' + 10;
                    else if(hex >= 'A' && hex <= 'F')
                        digit = hex - 'A' + 10;
                    else
                        return 1; // Invalid hex digit
                    codepoint = (codepoint << 4) | digit;
                }

                // Handle UTF-16 surrogate pairs
                if(codepoint >= 0xD800 && codepoint <= 0xDBFF){
                    // High surrogate, expect low surrogate
                    if(in_pos + 6 > length || escaped[in_pos] != '\\' || escaped[in_pos+1] != 'u')
                        return 1;
                    in_pos += 2;

                    uint32_t low_surrogate = 0;
                    for(int i = 0; i < 4; i++){
                        char hex = escaped[in_pos++];
                        uint32_t digit;
                        if(hex >= '0' && hex <= '9')
                            digit = hex - '0';
                        else if(hex >= 'a' && hex <= 'f')
                            digit = hex - 'a' + 10;
                        else if(hex >= 'A' && hex <= 'F')
                            digit = hex - 'A' + 10;
                        else
                            return 1;
                        low_surrogate = (low_surrogate << 4) | digit;
                    }

                    if(low_surrogate < 0xDC00 || low_surrogate > 0xDFFF)
                        return 1; // Invalid low surrogate

                    // Combine surrogates into codepoint
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low_surrogate - 0xDC00);
                }

                // Encode as UTF-8
                if(codepoint <= 0x7F){
                    outstring[out_pos++] = (char)codepoint;
                }
                else if(codepoint <= 0x7FF){
                    outstring[out_pos++] = (char)(0xC0 | (codepoint >> 6));
                    outstring[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                }
                else if(codepoint <= 0xFFFF){
                    outstring[out_pos++] = (char)(0xE0 | (codepoint >> 12));
                    outstring[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    outstring[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                }
                else if(codepoint <= 0x10FFFF){
                    outstring[out_pos++] = (char)(0xF0 | (codepoint >> 18));
                    outstring[out_pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                    outstring[out_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                    outstring[out_pos++] = (char)(0x80 | (codepoint & 0x3F));
                }
                else {
                    return 1; // Invalid codepoint
                }
                break;
            }
            default:
                return 1; // Invalid escape sequence
        }
    }

    *outlength = out_pos;
    return 0;
}

// Normalize user input by doing liberal unescape + strict escape in single pass
// This handles user input from TUI where:
// - Valid escape sequences (\n, \t, etc.) should be preserved
// - Invalid escape sequences (\x) should have backslash escaped
// - Unescaped special chars (") should be escaped
// Returns: 0 on success, 1 on error
// outstring must have at least 'length * 2' bytes available (worst case)
// *outlength will be set to actual output length
DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_normalize_user_input(const char* restrict input, size_t length, char* restrict outstring, size_t* restrict outlength){
    if(!outlength) return 1;
    if(!length){
        *outlength = 0;
        return 0;
    }
    if(!input || !outstring) return 1;

    size_t in_pos = 0;
    size_t out_pos = 0;

    while(in_pos < length){
        char c = input[in_pos];

        if(c == '\\'){
            // Check if this is a valid escape sequence
            if(in_pos + 1 < length){
                char next = input[in_pos + 1];
                // Check for valid JSON escape characters
                if(next == '"' || next == '\\' || next == '/' ||
                   next == 'b' || next == 'f' || next == 'n' ||
                   next == 'r' || next == 't'){
                    // Valid escape sequence - pass through as-is
                    outstring[out_pos++] = c;
                    outstring[out_pos++] = next;
                    in_pos += 2;
                    continue;
                }
                else if(next == 'u'){
                    // Unicode escape sequence \uXXXX
                    // Validate that we have 4 hex digits
                    if(in_pos + 5 < length){
                        int valid = 1;
                        for(int i = 0; i < 4; i++){
                            char hex = input[in_pos + 2 + i];
                            if(!((hex >= '0' && hex <= '9') ||
                                 (hex >= 'a' && hex <= 'f') ||
                                 (hex >= 'A' && hex <= 'F'))){
                                valid = 0;
                                break;
                            }
                        }
                        if(valid){
                            // Valid unicode escape - pass through
                            outstring[out_pos++] = '\\';
                            outstring[out_pos++] = 'u';
                            outstring[out_pos++] = input[in_pos + 2];
                            outstring[out_pos++] = input[in_pos + 3];
                            outstring[out_pos++] = input[in_pos + 4];
                            outstring[out_pos++] = input[in_pos + 5];
                            in_pos += 6;
                            continue;
                        }
                    }
                    // Invalid unicode escape - fall through to escape the backslash
                }
                // Invalid escape sequence - escape the backslash
                outstring[out_pos++] = '\\';
                outstring[out_pos++] = '\\';
                in_pos++;
                continue;
            }
            else {
                // Trailing backslash - escape it
                outstring[out_pos++] = '\\';
                outstring[out_pos++] = '\\';
                in_pos++;
                continue;
            }
        }
        else if(c == '"'){
            // Unescaped quote - needs escaping
            outstring[out_pos++] = '\\';
            outstring[out_pos++] = '"';
            in_pos++;
        }
        else if((unsigned char)c < 32){
            // Control character - escape it
            switch(c){
                case '\b':
                    outstring[out_pos++] = '\\';
                    outstring[out_pos++] = 'b';
                    break;
                case '\f':
                    outstring[out_pos++] = '\\';
                    outstring[out_pos++] = 'f';
                    break;
                case '\n':
                    outstring[out_pos++] = '\\';
                    outstring[out_pos++] = 'n';
                    break;
                case '\r':
                    outstring[out_pos++] = '\\';
                    outstring[out_pos++] = 'r';
                    break;
                case '\t':
                    outstring[out_pos++] = '\\';
                    outstring[out_pos++] = 't';
                    break;
                default:{
                    // Other control chars - use \u00xx format
                    const char* const hex = "0123456789abcdef";
                    outstring[out_pos++] = '\\';
                    outstring[out_pos++] = 'u';
                    outstring[out_pos++] = '0';
                    outstring[out_pos++] = '0';
                    outstring[out_pos++] = hex[((unsigned char)c & 0xf0) >> 4];
                    outstring[out_pos++] = hex[((unsigned char)c & 0xf)];
                    break;
                }
            }
            in_pos++;
        }
        else {
            // Normal character - pass through
            outstring[out_pos++] = c;
            in_pos++;
        }
    }

    *outlength = out_pos;
    return 0;
}

// Normalize user input and atomize it in one operation
// This is the common pattern: normalize user input from editor/UI, then intern it
// Handles memory allocation internally for the normalization buffer
// Returns: 0 on success, 1 on error
DRJSON_API
DRJSON_WARN_UNUSED
int
drjson_normalize_and_atomize(DrJsonContext* ctx, const char* restrict input, size_t length, DrJsonAtom* outatom){
    if(!outatom) return 1;
    if(!length){
        return drjson_atomize(ctx, "", 0, outatom);
    }
    if(!input) return 1;

    // Allocate buffer for normalization (worst case: length * 2)
    size_t buffer_size = length * 2;
    char* normalized = drj_alloc(ctx, buffer_size);
    if(!normalized) return 1;

    size_t normalized_len;
    int err = drjson_normalize_user_input(input, length, normalized, &normalized_len);
    if(err){
        drj_free(ctx, normalized, buffer_size);
        return err;
    }

    // Atomize the normalized string
    err = drjson_atomize(ctx, normalized, normalized_len, outatom);
    drj_free(ctx, normalized, buffer_size);
    return err;
}

DRJSON_API
void
drjson_get_line_column(const DrJsonParseContext* ctx, size_t* line, size_t* column){
    size_t lin = 0;
    size_t col = 0;
    // just do it the slow way, whatever.
    for(const char* c = ctx->begin; c != ctx->cursor; c++){
        switch(*c){
            case '\n':
                lin++;
                col = 0;
                break;
            default:
                col++;
                break;
        }
    }
    *line = lin;
    *column = col;
}

DRJSON_API
void
drjson_ctx_free_all(DrJsonContext* ctx){
    if(ctx->allocator.free_all){
        ctx->allocator.free_all(ctx->allocator.user_pointer);
        return;
    }
    if(!ctx->allocator.free)
        return;
    for(size_t i = 0; i < ctx->atoms.count; i++){
        DrjAtomStr* astrs = ctx->atoms.data;
        DrjAtomStr* a = &astrs[i];
        if(!a->allocated) continue;
        drj_free(ctx, a->pointer, a->length);
    }
    ctx->allocator.free(ctx->allocator.user_pointer, ctx->atoms.data, drj_atom_table_size_for(ctx->atoms.capacity));
    // XXX
    //  We don't free the atom table or the atoms!
    // Release strings

    // Free each object
    for(size_t i = 0; i < ctx->objects.count; i++){
        DrJsonObject* odata = ctx->objects.data;
        DrJsonObject* o = &odata[i];
        if(o->capacity)
            drj_free(ctx, o->object_items, drjson_size_for_object_of_length(o->capacity));
    }
    // Then the objects array
    if(ctx->objects.data)
        drj_free(ctx, ctx->objects.data, ctx->objects.capacity*sizeof (DrJsonObject));
    // Free interned objects
    if(ctx->interned_objects.capacity)
        drj_free(ctx, ctx->interned_objects.data, ctx->interned_objects.capacity*sizeof(uint32_t)*4);

    // Free each array
    for(size_t i = 0; i < ctx->arrays.count; i++){
        DrJsonArray* adata = ctx->arrays.data;
        DrJsonArray* a = &adata[i];
        if(a->capacity)
            drj_free(ctx, a->array_items,a->capacity*sizeof *a->array_items);
    }
    // Free arrays array
    if(ctx->arrays.data)
        drj_free(ctx, ctx->arrays.data, ctx->arrays.capacity*sizeof(DrJsonArray));

    // Free interned arrays
    if(ctx->interned_arrays.capacity)
        drj_free(ctx, ctx->interned_arrays.data, ctx->interned_arrays.capacity*sizeof(uint32_t)*4);

    #ifndef DRJ_DONT_FREE_CTX
        drj_free(ctx, ctx, sizeof *ctx);
    #endif
}


DRJSON_API
DrJsonValue
drjson_make_string(DrJsonContext* ctx, const char* s, size_t length){
    DrJsonAtom atom;
    int err = drj_atomize_str(&ctx->atoms, &ctx->allocator, s, (uint32_t)length, 1, &atom);
    if(err)
        return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
    return drjson_atom_to_value(atom);
}

static const char*_Nonnull const DrJsonKindNames[] = {
    [DRJSON_ERROR]         = "error",
    [DRJSON_NUMBER]        = "number",
    [DRJSON_INTEGER]       = "integer",
    [DRJSON_UINTEGER]      = "uinteger",
    [DRJSON_STRING]        = "string",
    [DRJSON_ARRAY]         = "array",
    [DRJSON_OBJECT]        = "object",
    [DRJSON_NULL]          = "null",
    [DRJSON_BOOL]          = "bool",
    [DRJSON_ARRAY_VIEW]    = "array view",
    [DRJSON_OBJECT_KEYS]   = "object keys",
    [DRJSON_OBJECT_VALUES] = "object values",
    [DRJSON_OBJECT_ITEMS]  = "object items",
};

static const size_t DrJsonKindNameLengths[] = {
    [DRJSON_ERROR]         = sizeof("error")-1,
    [DRJSON_NUMBER]        = sizeof("number")-1,
    [DRJSON_INTEGER]       = sizeof("integer")-1,
    [DRJSON_UINTEGER]      = sizeof("uinteger")-1,
    [DRJSON_STRING]        = sizeof("string")-1,
    [DRJSON_ARRAY]         = sizeof("array")-1,
    [DRJSON_OBJECT]        = sizeof("object")-1,
    [DRJSON_NULL]          = sizeof("null")-1,
    [DRJSON_BOOL]          = sizeof("bool")-1,
    [DRJSON_ARRAY_VIEW]    = sizeof("array view")-1,
    [DRJSON_OBJECT_KEYS]   = sizeof("object keys")-1,
    [DRJSON_OBJECT_VALUES] = sizeof("object values")-1,
    [DRJSON_OBJECT_ITEMS]  = sizeof("object items")-1,
};

static const char*_Nonnull const DrJsonErrorNames[] = {
    [DRJSON_ERROR_NONE]           = "No error",
    [DRJSON_ERROR_UNEXPECTED_EOF] = "Unexpected End of Input",
    [DRJSON_ERROR_ALLOC_FAILURE]  = "Allocation Failure",
    [DRJSON_ERROR_MISSING_KEY]    = "Missing Key",
    [DRJSON_ERROR_INDEX_ERROR]    = "Index Error",
    [DRJSON_ERROR_INVALID_CHAR]   = "Invalid Char",
    [DRJSON_ERROR_INVALID_VALUE]  = "Invalid Value",
    [DRJSON_ERROR_TOO_DEEP]       = "Too Many Levels of Nesting",
    [DRJSON_ERROR_TYPE_ERROR]     = "Invalid type for operation",
    [DRJSON_ERROR_INVALID_ERROR]  = "Error is Invalid",
    [DRJSON_ERROR_TRAILING_CONTENT] = "Trailing Content After Value",
};

static const size_t DrJsonErrorNameLengths[] = {
    [DRJSON_ERROR_NONE]           = sizeof("No error")-1,
    [DRJSON_ERROR_UNEXPECTED_EOF] = sizeof("Unexpected End of Input")-1,
    [DRJSON_ERROR_ALLOC_FAILURE]  = sizeof("Allocation Failure")-1,
    [DRJSON_ERROR_MISSING_KEY]    = sizeof("Missing Key")-1,
    [DRJSON_ERROR_INDEX_ERROR]    = sizeof("Index Error")-1,
    [DRJSON_ERROR_INVALID_CHAR]   = sizeof("Invalid Char")-1,
    [DRJSON_ERROR_INVALID_VALUE]  = sizeof("Invalid Value")-1,
    [DRJSON_ERROR_TOO_DEEP]       = sizeof("Too Many Levels of Nesting")-1,
    [DRJSON_ERROR_TYPE_ERROR]     = sizeof("Invalid type for operation")-1,
    [DRJSON_ERROR_INVALID_ERROR]  = sizeof("Error is Invalid")-1,
    [DRJSON_ERROR_TRAILING_CONTENT] = sizeof("Trailing Content After Value")-1,
};

static inline
const char*
drjson_get_error_name(DrJsonValue v){
    return DrJsonErrorNames[v.error_code];
}

static inline
size_t
drjson_get_error_name_length(DrJsonValue v){
    return DrJsonErrorNameLengths[v.error_code];
}

DRJSON_API
const char*
drjson_error_name(DrJsonErrorCode code, size_t*_Nullable length){
    if(code < 0  || code > DRJSON_ERROR_INVALID_ERROR)
        code = DRJSON_ERROR_INVALID_ERROR;
    if(length) *length = DrJsonErrorNameLengths[code];
    return DrJsonErrorNames[code];
}

DRJSON_API
const char*
drjson_kind_name(DrJsonKind kind, size_t*_Nullable length){
    if(kind < 0 || kind > DRJSON_OBJECT_ITEMS)
        kind = DRJSON_ERROR;
    if(length) *length = DrJsonKindNameLengths[kind];
    return DrJsonKindNames[kind];
}

static
void
drj_mark(DrJsonContext* ctx, DrJsonValue v){
#ifdef DRJ_DEBUG
    fprintf(stderr, "mark\n");
#endif
    switch(v.kind){
        case DRJSON_OBJECT:
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_ITEMS:
        case DRJSON_OBJECT_VALUES:{
            DrJsonObject* odata = ctx->objects.data;
            DrJsonObject* object = &odata[v.object_idx];
            if(!object->capacity) return;
            if(object->marked) return;
            object->marked = 1;
            DrJsonObjectPair* pairs = drj_obj_get_pairs(object->object_items, object->capacity);
            for(size_t i = 0; i < object->count; i++)
                drj_mark(ctx, pairs[i].value);
        }break;

        case DRJSON_ARRAY:
        case DRJSON_ARRAY_VIEW:{
            DrJsonArray* adata = ctx->arrays.data;
            DrJsonArray* array = &adata[v.array_idx];
            if(!array->capacity) return;
            if(array->marked) return;
            array->marked = 1;
            for(size_t i = 0; i < array->count; i++)
                drj_mark(ctx, array->array_items[i]);
        }break;
        default:
            return;
    }
}

#define DRJ_FREE_IDX (UINT32_MAX-1)
static
void
drj_free_obj(DrJsonContext* ctx, DrJsonObject* o){
    size_t o_idx = o - ctx->objects.data;
    #ifdef DRJ_DEBUG
    fprintf(stderr, "Freeing object: %p (%zu)\n", (void*)o, o_idx);
    assert(!o->freed);
    o->freed = 1;
    #endif
    if(o->read_only){
        o->read_only = 0;
        uint32_t hash = hash_align8(o->object_items, o->count*sizeof(DrJsonObjectPair));
        uint32_t idx = fast_reduce32(hash, (uint32_t)ctx->interned_objects.capacity*2);
        uint32_t* idxes = (uint32_t*)(ctx->interned_objects.data+ctx->interned_objects.capacity);
        DrjHashIdx* hi = ctx->interned_objects.data;
        #ifdef DRJ_DEBUG
        uint32_t found_idx = -1;
        for(size_t i = 0; i < 2*ctx->interned_objects.capacity; i++){
            if(hi[i].idx == (uint32_t)o_idx){
                found_idx = (uint32_t)i;
                break;
            }
        }
        assert(found_idx != (uint32_t)-1);
        #endif
        for(;;){
            uint32_t i = idxes[idx];
            assert(i != UINT32_MAX);
            if(i != UINT32_MAX && hi[i].idx == (uint32_t)o_idx){
                #ifdef DRJ_DEBUG
                assert(i == found_idx);
                #endif
                assert(hi[i].hash == hash);
                hi[i].idx = DRJ_FREE_IDX;
                break;
            }
            idx++;
            if(idx == (uint32_t)(2*ctx->interned_objects.capacity)) idx = 0;
        }
    }
    if(o->capacity){
        drj_free(ctx, o->object_items, drjson_size_for_object_of_length(o->capacity));
        o->object_items = NULL;
        o->capacity = 0;
    }
    if(o_idx == ctx->objects.count-1){
        ctx->objects.count--;
    }
    else {
        if(o_idx <= UINT32_MAX/2 && o_idx){
            o->count = ctx->objects.free_object;
            ctx->objects.free_object = o_idx;
        }
    }
}

static
void
drj_free_array(DrJsonContext* ctx, DrJsonArray* a){
    size_t a_idx = a - ctx->arrays.data;
#ifdef DRJ_DEBUG
    fprintf(stderr, "Freeing array: %p (%zu)\n", (void*)a, a_idx);
    assert(!a->freed);
    a->freed = 1;
#endif
    if(a->read_only){
        a->read_only = 0;
        // fprintf(stderr, "Freeing interned array: %p (%zu)\n", a, a_idx);
        uint32_t hash = hash_align8(a->array_items, a->count*sizeof *a->array_items);
        uint32_t idx = fast_reduce32(hash, (uint32_t)ctx->interned_arrays.capacity*2);
        uint32_t* idxes = (uint32_t*)(ctx->interned_arrays.data+ctx->interned_arrays.capacity);
        DrjHashIdx* hi = ctx->interned_arrays.data;
        #ifdef DRJ_DEBUG
        uint32_t found_idx = -1;
        for(size_t i = 0; i < 2*ctx->interned_arrays.capacity; i++){
            if(hi[i].idx == (uint32_t)a_idx){
                found_idx = (uint32_t)i;
                break;
            }
        }
        assert(found_idx != (uint32_t)-1);
        #endif
        for(;;){
            uint32_t i = idxes[idx];
            assert(i != UINT32_MAX);
            if(i != UINT32_MAX && hi[i].idx == (uint32_t)a_idx){
                #ifdef DRJ_DEBUG
                assert(i == found_idx);
                #endif
                assert(hi[i].hash == hash);
                hi[i].idx = DRJ_FREE_IDX;
                break;
            }
            idx++;
            if(idx == (uint32_t)(2*ctx->interned_arrays.capacity)) idx = 0;
        }
    }
    if(a->capacity){
        drj_free(ctx, a->array_items, a->capacity*sizeof *a->array_items);
        a->array_items = NULL;
        a->capacity = 0;
    }
    if(a_idx == ctx->arrays.count-1){
        ctx->arrays.count--;
    }
    else {
        if(a_idx <= UINT32_MAX/2 && a_idx){
            a->count = ctx->arrays.free_array;
            ctx->arrays.free_array = a_idx;
        }
    }
}
static
void
drj_sweep(DrJsonContext* ctx){
#ifdef DRJ_DEBUG
    fprintf(stderr, "sweep\n");
#endif
    {
        DrJsonObject* odata = ctx->objects.data;
        for(size_t i = ctx->objects.count; i--;){
            DrJsonObject* o = &odata[i];
            if(!o->capacity) continue;
            if(o->marked){
                o->marked = 0;
                continue;
            }
            drj_free_obj(ctx, o);
        }
    }
    {
        DrJsonArray* adata = ctx->arrays.data;
        for(size_t i = ctx->arrays.count; i--;){
            DrJsonArray* a = &adata[i];
            if(!a->capacity) continue;
            if(a->marked){
                a->marked = 0;
                continue;
            }
            drj_free_array(ctx, a);

        }
    }
}

DRJSON_API
int
drjson_gc(DrJsonContext* ctx, const DrJsonValue*_Null_unspecified roots, size_t rootcount){
#ifdef DRJ_DEBUG
    fprintf(stderr, "gc\n");
#endif
    for(size_t i = 0; i < rootcount; i++)
        drj_mark(ctx, roots[i]);
    drj_sweep(ctx);
    return 0;
}

static
DrJsonValue
drj_dupe_array_ronly(DrJsonContext* ctx, DrJsonValue src_val){
    ssize_t new_idx = alloc_array(ctx);
    if(new_idx < 0) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom when duping array");
    assert(src_val.kind == DRJSON_ARRAY);
    DrJsonArray* src = &ctx->arrays.data[src_val.array_idx];
    DrJsonArray* dst = &ctx->arrays.data[new_idx];
    uint32_t cap = src->count;
    DrJsonValue* items = drj_alloc(ctx, cap * sizeof *items);
    if(!items) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom when duping array");
    *dst = (DrJsonArray){
        .count = cap,
        .capacity = cap,
        .array_items = items,
        .marked = 0,
        .read_only = 1,
    };
    drj_memcpy(items, src->array_items, cap*sizeof *items);
    return (DrJsonValue){.kind=DRJSON_ARRAY, .array_idx=new_idx};
}

static
DrJsonValue
drj_intern_array(DrJsonContext* ctx, DrJsonValue val, _Bool consume){
    assert(val.kind == DRJSON_ARRAY);
    DrJsonArray* adata = ctx->arrays.data;
    DrJsonArray* array = &adata[val.array_idx];
    if(array->read_only) return val;
    size_t count = array->count;
    DrJsonValue* items = array->array_items;
    for(size_t i = 0; i < count; i++){
        if(!drj_is_ro(ctx, items[i]))
            return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "All values of array must be read only to be interned");
    }
    // assume we're going to insert for purpose of checking capacity.
    if(ctx->interned_arrays.count == ctx->interned_arrays.capacity){
        size_t old_cap = ctx->interned_arrays.capacity;
        size_t new_cap = old_cap?old_cap*2:16;
        size_t old_size = sizeof(DrjHashIdx)*old_cap + sizeof(uint32_t)*2*old_cap;
        size_t new_size = sizeof(DrjHashIdx)*new_cap + sizeof(uint32_t)*2*new_cap;
        void* data = drj_realloc(ctx, ctx->interned_arrays.data, old_size, new_size);
        if(!data) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate while interning array");
        DrjHashIdx* hi = data;
        uint32_t* idxes = (uint32_t*)((char*)data+sizeof(DrjHashIdx)*new_cap);
        drj_memset(idxes, 0xff, sizeof(uint32_t)*2*new_cap);
        size_t count = 0;
        for(size_t i = 0; i < ctx->interned_arrays.count; i++){
            if(hi[i].idx == DRJ_FREE_IDX) continue;
            uint32_t hash = hi[i].hash;
            uint32_t idx = fast_reduce32(hash, (uint32_t)new_cap*2);
            while(idxes[idx] != UINT32_MAX){
                idx++;
                if(idx == new_cap*2) idx = 0;
            }
            if(i != count) hi[count] = hi[i];
            idxes[idx] = (uint32_t)count;
            count++;
        }
        ctx->interned_arrays.capacity = new_cap;
        ctx->interned_arrays.data = data;
        ctx->interned_arrays.count = count;
    }
    uint32_t hash = hash_align8(items, count * sizeof *items);
    uint32_t* idxes = (uint32_t*)(ctx->interned_arrays.data+ctx->interned_arrays.capacity);
    uint32_t idx = fast_reduce32(hash, (uint32_t)ctx->interned_arrays.capacity*2);
    DrjHashIdx* hi = ctx->interned_arrays.data;
    uint32_t first_free = -1; _Bool found_free = 0;
    for(;;){
        uint32_t i = idxes[idx];
        if(i == UINT32_MAX){
            if(found_free) idx = first_free;
            DrJsonValue cpy_val = consume?val:drj_dupe_array_ronly(ctx, val);
            if(consume)array->read_only = 1;
            if(cpy_val.kind == DRJSON_ERROR) return cpy_val;
            idxes[idx] = ctx->interned_arrays.count;
            hi[ctx->interned_arrays.count] = (DrjHashIdx){.hash=hash, .idx=cpy_val.array_idx};
            ctx->interned_arrays.count++;
            return cpy_val;
        }
        else if((hi[i].hash == hash && hi[i].idx == DRJ_FREE_IDX)){
            found_free = 1;
            first_free = idx;
        }
        else if(hi[i].hash == hash){
            uint32_t a_idx = hi[i].idx;
            DrJsonArray* a = &ctx->arrays.data[a_idx];
            if(a->count == array->count){
                if(!a->count || memcmp(a->array_items, array->array_items, a->count*sizeof *a->array_items) == 0){
                    if(consume) drj_free_array(ctx, array);
                    return (DrJsonValue){.kind=DRJSON_ARRAY, .array_idx=a_idx};
                }
            }
        }
        idx++;
        if(idx == ctx->interned_arrays.capacity*2) idx = 0;
    }

    return val;
}

static
DrJsonValue
drj_dupe_object_ronly(DrJsonContext* ctx, DrJsonValue src_val){
    assert(src_val.kind == DRJSON_OBJECT);
    ssize_t new_idx = alloc_obj(ctx);
    if(new_idx < 0) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom when duping object");
    DrJsonObject* src = &ctx->objects.data[src_val.object_idx];
    DrJsonObject* dst = &ctx->objects.data[new_idx];
    uint32_t cap = src->count;
    void* items = drj_alloc(ctx, drjson_size_for_object_of_length(cap));
    if(!items) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom when duping object");
    *dst = (DrJsonObject){
        .count = cap,
        .capacity = cap,
        .object_items = items,
        .marked = 0,
        .read_only = 1,
    };
    uint32_t* idxes = (uint32_t*)((char*)items + sizeof(DrJsonObjectPair)*cap);
    drj_memcpy(items, src->object_items, cap*sizeof(DrJsonObjectPair));
    drj_memset(idxes, 0xff, cap*2*sizeof *idxes);
    DrJsonObjectPair* pairs = items;
    for(size_t i = 0; i < cap; i++){
        DrjAtomStr str = drj_get_atom_str(&ctx->atoms, pairs[i].atom);
        uint32_t hash = str.hash;
        uint32_t idx = fast_reduce32(hash, cap*2);
        while(idxes[idx] != UINT32_MAX){
            idx++;
            if(idx == cap*2) idx = 0;
        }
        idxes[idx] = i;
    }
    return (DrJsonValue){.kind = DRJSON_OBJECT, .object_idx=new_idx};
}

static
DrJsonValue
drj_intern_object(DrJsonContext* ctx, DrJsonValue val, _Bool consume){
    assert(val.kind == DRJSON_OBJECT);
    DrJsonObject* odata = ctx->objects.data;
    DrJsonObject* object = &odata[val.object_idx];
    if(object->read_only) return val;
    size_t count = object->count;
    DrJsonObjectPair* pairs = object->object_items;
    for(size_t i = 0; i < count; i++){
        if(!drj_is_ro(ctx, pairs[i].value))
            return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "All values of object must be read only to be interned");
    }
    // assume we're going to insert for purpose of checking capacity.
    if(ctx->interned_objects.count == ctx->interned_objects.capacity){
        size_t old_cap = ctx->interned_objects.capacity;
        size_t new_cap = old_cap?old_cap*2:16;
        size_t old_size = sizeof(DrjHashIdx)*old_cap + sizeof(uint32_t)*2*old_cap;
        size_t new_size = sizeof(DrjHashIdx)*new_cap + sizeof(uint32_t)*2*new_cap;
        void* data = drj_realloc(ctx, ctx->interned_objects.data, old_size, new_size);
        if(!data) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate while interning object");
        DrjHashIdx* hi = data;
        uint32_t* idxes = (uint32_t*)((char*)data+sizeof(DrjHashIdx)*new_cap);
        drj_memset(idxes, 0xff, sizeof(uint32_t)*2*new_cap);
        size_t count = 0;
        for(size_t i = 0; i < ctx->interned_objects.count; i++){
            if(hi[i].idx == DRJ_FREE_IDX) continue;
            uint32_t hash = hi[i].hash;
            uint32_t idx = fast_reduce32(hash, (uint32_t)new_cap*2);
            while(idxes[idx] != UINT32_MAX){
                idx++;
                if(idx == new_cap*2) idx = 0;
            }
            if(i != count) hi[count] = hi[i];
            idxes[idx] = (uint32_t)count;
            count++;
        }
        ctx->interned_objects.capacity = new_cap;
        ctx->interned_objects.data = data;
        ctx->interned_objects.count = count;
    }
    uint32_t hash = hash_align8(pairs, count * sizeof *pairs);
    uint32_t* idxes = (uint32_t*)(ctx->interned_objects.data+ctx->interned_objects.capacity);
    uint32_t idx = fast_reduce32(hash, (uint32_t)ctx->interned_objects.capacity*2);
    DrjHashIdx* hi = ctx->interned_objects.data;
    uint32_t first_free = -1; _Bool found_free = 0;
    for(;;){
        uint32_t i = idxes[idx];
        if(i == UINT32_MAX){
            if(found_free) idx = first_free;
            DrJsonValue cpy = consume?val:drj_dupe_object_ronly(ctx, val);
            if(consume) object->read_only = 1;
            // DrJsonValue cpy = drj_dupe_object_ronly(ctx, val);
            if(cpy.kind == DRJSON_ERROR) return cpy;
            idxes[idx] = ctx->interned_objects.count;
            hi[ctx->interned_objects.count] = (DrjHashIdx){.hash=hash, .idx=cpy.object_idx};
            ctx->interned_objects.count++;
            return cpy;
        }
        else if((hi[i].hash == hash && hi[i].idx == DRJ_FREE_IDX)){
            found_free = 1;
            first_free = idx;
            // printf("Empty slot\n");
        }
        else if(hi[i].hash == hash){
            uint32_t o_idx = hi[i].idx;
            DrJsonObject* o = &ctx->objects.data[o_idx];
            if(o->count == object->count){
                if(memcmp(o->object_items, object->object_items, o->count*sizeof(DrJsonObjectPair)) == 0){
                    if(consume) drj_free_obj(ctx, object);
                    return (DrJsonValue){.kind=DRJSON_OBJECT, .object_idx=o_idx};
                }
            }
        }
        idx++;
        if(idx == ctx->interned_objects.capacity*2) idx = 0;
    }

    return val;
}


static
_Bool
drj_is_ro(DrJsonContext* ctx, DrJsonValue val){
    switch(val.kind){
        case DRJSON_ARRAY_VIEW:
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_VALUES:
        case DRJSON_OBJECT_ITEMS: return 0;
        case DRJSON_OBJECT:{
            DrJsonObject* odata = ctx->objects.data;
            DrJsonObject* object = &odata[val.object_idx];
            return object->read_only;
        }break;
        case DRJSON_ARRAY:{
            DrJsonArray* adata = ctx->arrays.data;
            DrJsonArray* array = &adata[val.array_idx];
            return array->read_only;
        }break;
        default: return 1;
    }
}

DRJSON_API
DrJsonValue
drjson_intern_value(DrJsonContext* ctx, DrJsonValue val, _Bool consume){
    switch(val.kind){
        case DRJSON_ARRAY_VIEW:
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_VALUES:
        case DRJSON_OBJECT_ITEMS: return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "Cannot intern this type");
        case DRJSON_ARRAY: return drj_intern_array(ctx, val, consume);
        case DRJSON_OBJECT: return drj_intern_object(ctx, val, consume);
        default: return val;
    }
}


#ifdef DRJ_HAVE_SIMD
#undef DRJ_HAVE_SIMD
#undef DRJ_SIMD_WIDTH
#endif

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#include "fpconv/src/fpconv.c"

#endif
