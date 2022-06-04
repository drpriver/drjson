//
// Copyright © 2021-2022, David Priver
//
#ifndef ALLOCATOR_C
#define ALLOCATOR_C
#include <stddef.h>
// abort
#include <stdlib.h>
#include "allocator.h"
#include "linear_allocator.h"
#include "mallocator.h"
#include "recording_allocator.h"
#include "arena_allocator.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

#ifndef unreachable
#if defined(__GNUC__) || defined(__clang__)
#define unreachable() __builtin_unreachable()
#else
#define unreachable() __assume(0)
#endif
#endif

static inline
void
Allocator_free_all(Allocator a){
    switch(a.type){
        case ALLOCATOR_UNSET:
            abort();
            return;
        case ALLOCATOR_MALLOC:
            abort();
            return;
        case ALLOCATOR_LINEAR:
            linear_reset(a._data);
            return;
        case ALLOCATOR_RECORDED:
            recording_free_all(a._data);
            return;
        case ALLOCATOR_ARENA:
            ArenaAllocator_free_all(a._data);
            return;
    }
    abort();
}

MALLOC_FUNC
static inline
warn_unused
// force_inline
void*
Allocator_alloc(Allocator a, size_t size){
    switch(a.type){
        case ALLOCATOR_UNSET:
            abort();
            break;
        case ALLOCATOR_LINEAR:
            return linear_alloc(a._data, size);
        case ALLOCATOR_MALLOC:
            return malloc(size);
        case ALLOCATOR_RECORDED:
            return recording_alloc(a._data, size);
        case ALLOCATOR_ARENA:
            return ArenaAllocator_alloc(a._data, size);
    }
    abort();
    unreachable();
}

MALLOC_FUNC
static inline
warn_unused
// force_inline
void*
Allocator_zalloc(Allocator a, size_t size){
    switch(a.type){
        case ALLOCATOR_UNSET:
            abort();
            break;
        case ALLOCATOR_LINEAR:
            return linear_zalloc(a._data, size);
        case ALLOCATOR_MALLOC:
            return calloc(1, size);
        case ALLOCATOR_RECORDED:
            return recording_zalloc(a._data, size);
        case ALLOCATOR_ARENA:
            return ArenaAllocator_zalloc(a._data, size);
    }
    abort();
    unreachable();
}

static inline
// force_inline
warn_unused
void*
Allocator_realloc(Allocator a, void*_Nullable data, size_t orig_size, size_t size){
    switch(a.type){
        case ALLOCATOR_UNSET:
            abort();
            break;
        case ALLOCATOR_LINEAR:
            return linear_realloc(a._data, data, orig_size, size);
        case ALLOCATOR_MALLOC:
            return sane_realloc(data, orig_size, size);
        case ALLOCATOR_RECORDED:
            return recording_realloc(a._data, data, orig_size, size);
        case ALLOCATOR_ARENA:
            return (void*)ArenaAllocator_realloc(a._data, data, orig_size, size);
    }
    abort();
    unreachable();
}

static inline
// force_inline
void
Allocator_free(Allocator a, const void*_Nullable data, size_t size){
    switch(a.type){
        case ALLOCATOR_UNSET:
            abort();
            return;
        case ALLOCATOR_LINEAR:
            linear_free(a._data, data, size);
            return;
        case ALLOCATOR_MALLOC:
            const_free(data);
            return;
        case ALLOCATOR_RECORDED:
            recording_free(a._data, data, size);
            return;
        case ALLOCATOR_ARENA:
            return;
    }
    abort();
}

static inline
// force_inline
size_t
Allocator_good_size(Allocator a, size_t size){
    switch(a.type){
        case ALLOCATOR_UNSET:
            abort();
            return size;
        case ALLOCATOR_LINEAR:
            return size;
        case ALLOCATOR_RECORDED:
            // fall-through
        case ALLOCATOR_MALLOC:
        #ifdef __APPLE__
            return malloc_good_size(size);
        #else
            return size;
        #endif
        case ALLOCATOR_ARENA:
            return ArenaAllocator_round_size_up(size);
    }
    abort();
}

static inline
warn_unused
// force_inline
void*
Allocator_dupe(Allocator allocator, const void* data, size_t size){
    void* result = Allocator_alloc(allocator, size);
    memcpy(result, data, size);
    return result;
}

MALLOC_FUNC
static inline
warn_unused
char*
Allocator_strndup(Allocator allocator, const char* str, size_t length){
    char* result = Allocator_alloc(allocator, length+1);
    unhandled_error_condition(!result);
    if(length)
        memcpy(result, str, length);
    result[length] = '\0';
    return result;
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
