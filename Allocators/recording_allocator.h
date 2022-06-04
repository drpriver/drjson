//
// Copyright © 2021-2022, David Priver
//
#ifndef RECORDING_ALLOCATOR_H
#define RECORDING_ALLOCATOR_H
#include <stdlib.h>
#include <string.h>
#include "allocator.h"

// This stuff is for debugging alloc/free errors.
#if 0
#define DEBUG_ALLOCATIONS
#endif

#ifdef DEBUG_ALLOCATIONS
#include <stdio.h>
#define RA_LOGIT(fmt, ...) fprintf(stderr, "%s:%d:" fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define RA_LOGIT(...) (void)0
#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Nonnull
#define _Nonnull
#endif
#endif

/*
 * An Allocator wrapper allocator that tracks all allocations
 * and frees done through it so as to provide a free_all function
 * for allocators that don't normally support it.
 *
 * This is currently super unoptimized, but that's ok.
 */

#ifndef sane_realloc
// Realloc's signature is silly which makes it hard to
// reimplement in a sane way. So in order to accomodate
// platforms where we need to implement it ourselves
// (aka WASM), we use this compatibility macro.
#ifndef WASM
#define sane_realloc(ptr, orig_size, size) realloc(ptr, size)
#else
static void* sane_realloc(void* ptr, size_t orig_size, size_t size);
#endif
#endif


typedef struct RecordingAllocator RecordingAllocator;
struct RecordingAllocator{
    // We need a dynamic array to record all of the allocations.
    // We specialize it to be SOA
    void*_Nullable*_Nonnull allocations;
    size_t* allocation_sizes;
    size_t count;
    size_t capacity;
};

static inline
void
recording_ensure_capacity(RecordingAllocator* r){
    if(r->count < r->capacity)
        return;
    if(!r->capacity){
        enum {INITIAL_CAPACITY=32};
        r->capacity = INITIAL_CAPACITY;
        r->allocations = malloc(INITIAL_CAPACITY*sizeof(*r->allocations));
        r->allocation_sizes = malloc(INITIAL_CAPACITY*sizeof(*r->allocation_sizes));
        return;
    }
    size_t old_cap = r->capacity;
    size_t new_cap = old_cap * 2;
    r->allocations = sane_realloc(r->allocations, old_cap * sizeof(*r->allocations), new_cap*sizeof(*r->allocations));
    r->allocation_sizes = sane_realloc(r->allocation_sizes, old_cap*sizeof(*r->allocation_sizes), new_cap*sizeof(*r->allocation_sizes));
    r->capacity = new_cap;
}

MALLOC_FUNC
static
warn_unused
void*
recording_alloc(RecordingAllocator* r, size_t size){
    RA_LOGIT("Allocation for %zu requested.", size);
    void* result = malloc(size);
    RA_LOGIT("Allocation for %zu granted: %p", size, result);
    if(!result)
        return result;
    recording_ensure_capacity(r);
    size_t index = r->count++;
    r->allocations[index] = result;
    r->allocation_sizes[index] = size;
    return result;
}

MALLOC_FUNC
static
warn_unused
void*
recording_zalloc(RecordingAllocator* r, size_t size){
    RA_LOGIT("ZAllocation for %zu requested.", size);
    void* result = calloc(1, size);
    RA_LOGIT("ZAllocation for %zu granted: %p", size, result);
    if(!result)
        return result;
    recording_ensure_capacity(r);
    size_t index = r->count++;
    r->allocations[index] = result;
    r->allocation_sizes[index] = size;
    return result;
}

static
void
recording_free(RecordingAllocator* r, const void*_Nullable data, size_t size){
    if(!data){
        RA_LOGIT("Free with no data: %p, %zu", data, size);
        return;
    }
    RA_LOGIT("Free: %p, %zu", data, size);
    size_t count = r->count;
    if(!count)
        goto Lerror;
    if(data == r->allocations[count-1]){
        RA_LOGIT("Found the allocation");
        unhandled_error_condition(r->allocation_sizes[count-1] != size);
        const_free(data);
        r->count--;
        return;
    }
    // inefficient, but whatever
    for(size_t i = 0; i < count-1; i++){
        if(data == r->allocations[i]){
            RA_LOGIT("old ptr, size: %p, %zu", r->allocations[i], r->allocation_sizes[i]);
            unhandled_error_condition(size != r->allocation_sizes[i]);
            const_free(data);
            RA_LOGIT("Free succeeded: %p, %zu", data, size);
            // compact
            r->allocations[i] = r->allocations[count-1];
            r->allocation_sizes[i] = r->allocation_sizes[count-1];
            r->count--;
            return;
        }
    }
    Lerror:;
    assert(!(_Bool)"Freeing pointer not tracked by this allocator.");
}

// The money function, the reason we did this in the first
// place.
static
void
recording_free_all(RecordingAllocator* r){
    RA_LOGIT("Freeing all");
    for(size_t i = 0; i < r->count; i++){
        if(!r->allocations[i])
            continue;
        free(r->allocations[i]);
    }
    r->count = 0;
}

static inline
void*
recording_realloc(RecordingAllocator* r, void*_Nullable data, size_t orig_size, size_t new_size){
    RA_LOGIT("realloc request: old ptr: %p, orig_size: %zu, new_size: %zu", data, orig_size, new_size);
    if(!data)
        goto Lrealloc;
    size_t count = r->count;
    if(!count)
        goto Lrealloc;
    // check to see if we are reallocing in a loop.
    if(data == r->allocations[count-1]){
        RA_LOGIT("Was the last allocation");
        unhandled_error_condition(orig_size != r->allocation_sizes[count-1]);
        r->count--;
        goto Lrealloc;
    }
    for(size_t i = 0; i < count-1; i++){
        if(data == r->allocations[i]){
            unhandled_error_condition(orig_size != r->allocation_sizes[i]);
            // compact
            r->allocations[i] = r->allocations[count-1];
            r->allocation_sizes[i] = r->allocation_sizes[count-1];
            r->count--;
            break;
        }
    }
    Lrealloc:;
    RA_LOGIT("Falling back to actual realloc");
    void* result = sane_realloc(data, orig_size, new_size);
    recording_ensure_capacity(r);
    size_t index = r->count++;
    r->allocations[index] = result;
    r->allocation_sizes[index] = new_size;
    RA_LOGIT("Realloced %p, size %zu into %p, size %zu", data, orig_size, result, new_size);
    return result;
}

static
void
recording_cleanup(RecordingAllocator* r){
    RA_LOGIT("Cleaning up the recorder itself");
    free(r->allocation_sizes);
    free(r->allocations);
    memset(r, 0, sizeof(*r));
}

static inline
void
shallow_free_recorded_mallocator(Allocator a){
    RecordingAllocator* r = a._data;
    recording_cleanup(r);
    const_free(r);
}

static inline
Allocator
new_recorded_mallocator(void){
    RecordingAllocator* ra = calloc(1, sizeof(*ra));
    return (Allocator){
        ._data = ra,
        .type = ALLOCATOR_RECORDED,
    };
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
