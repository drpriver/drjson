//
// Copyright © 2021-2022, David Priver
//
#ifndef LINEAR_ALLOCATOR_H
#define LINEAR_ALLOCATOR_H
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "allocator.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
#endif
#endif

//
// A very simple allocator. All allocations are just advancing down a pointer in
// a linear fashion. We support freeing, but only if it was the most recent
// allocation (if pointer + size == cursor, we can just decrement the cursor).
//
// Because of such minimal book-keeping, it is very fast. It also supports
// freeing all the pointers allocated by it (as that is just a manner of setting
// cursor to 0).
//
// If you have long lived allocations or need to realloc at random times, it
// performs poorly. Realloc is particulary bad unless you can guarantee that
// nothing else has been allocated from this structure since you have realloced.
// In that case it's actually really awesome as it can perfectly reallocate
// in place with no copy. But if you need to realloc at random times it
// will always copy, which is a disaster.
//
// If capacity is exceeded, falls back to mallocing the data and storing the
// overflow allocations in a linked list, which isn't great so don't overflow.
//
struct OverflowAllocation {
    struct OverflowAllocation*_Nullable next;
    char buff[];
};
typedef struct LinearAllocator LinearAllocator;

struct LinearAllocator{
    // The buffer to allocate from.
    void*_Null_unspecified  _data;
    // How big the buffer is.
    size_t _capacity; // if over _capacity, we start mallocing
    // How many bytes have been allocated currently.
    size_t _cursor;
    // The greatest total number of bytes ever allocated by this allocator.
    size_t high_water;
    // The name of this allocator. Used for logging when we exceed capacity.
    const char*_Nullable name; // for logging purposes
    struct OverflowAllocation*_Nullable overflow;
};

//
// Mallocs a block of memory and returns the linear allocator that manages that
// block of memory. Call destroy_linear_storage when you're done with it.
//
// The name parameter needs to live for at least as long as the LinearAllocator
// as we do not copy it. Almost always it's a string literal so that is no
// worry.
//
static inline
warn_unused
LinearAllocator
new_linear_storage(size_t size, const char*_Nullable name){
    // Malloc has to return a pointer suitably aligned for any object,
    // so we don't have to do any alignment fixup.
    void* _data = malloc(size);
    unhandled_error_condition(!_data);
    return (LinearAllocator){
        ._data = _data,
        ._capacity=size,
        ._cursor=0,
        .high_water=0,
        .name = name,
    };
}

//
// Effectively frees all outstanding pointers by setting the cursor to 0 as if
// we had not allocated at all. It's super awesome to use this allocator if you
// know your needs will fit inside of its size and you have known points in
// your algorithm where you can just forget large numbers of pointers. For
// example, in a video game you can allocate pretty freely from a linear
// allocator for objects that only will exist during that frame and then reset
// it every frame.
//
static inline
void
linear_reset(LinearAllocator* s){
    s->_cursor = 0;
}

//
// If alloced via malloc, cleans-up the resources.
//
static inline
void
destroy_linear_storage(LinearAllocator* s){
    free(s->_data);
    for(struct OverflowAllocation*oa = s->overflow;oa;){
        struct OverflowAllocation* next = oa->next;
        free(oa);
        oa = next;
    }
    s->overflow = NULL;
    s->name = NULL;
    s->_data = NULL;
    s->_capacity = 0;
    s->_cursor = 0;
}

//
// Allocates a buffer of size size, suitably aligned to alignment.
//
MALLOC_FUNC
static
warn_unused
void*
linear_aligned_alloc(LinearAllocator* restrict s, size_t size, size_t alignment){
    uintptr_t val = (uintptr_t)s->_data;
    val += s->_cursor;
    // alignment is always a power of 2
    size_t align_mod = val & (alignment - 1);
    if(align_mod){
        s->_cursor += alignment - align_mod;
    }
    if(s->_cursor + size > s->_capacity){
        // fall back to malloc
#ifdef ERROR
        ERROR("Exceeded temporary storage capacity for '%s'! Wanted an additional %zu bytes, but only %zu left.\n", s->name?:"(unnamed)", size, s->_capacity - s->_cursor);
#endif
        s->high_water = s->_cursor + size;
        // leak
        struct OverflowAllocation* result =  malloc(size + sizeof(struct OverflowAllocation));
        unhandled_error_condition(!result);
        result->next = s->overflow;
        s->overflow = result;
        return result->buff;
    }
    void* result = ((char*)s->_data) + s->_cursor;
    s->_cursor += size;
    if(s->_cursor > s->high_water){
        s->high_water = s->_cursor;
    }
    return result;
}

//
// Allocates a buffer of size size, aligned to the generic alignment of 8.
// Technically this should be 16 for long doubles, but I literally never use
// those and having the minimum allocation size be 16 was too much for me.
//
MALLOC_FUNC
static
warn_unused
void*
linear_alloc(LinearAllocator* restrict s, size_t size){
    enum {GENERIC_ALIGNMENT = 8}; // lmao, but this allows for u64s on 32 bit platforms
    _Static_assert(sizeof(void*) <= GENERIC_ALIGNMENT, "");
    return linear_aligned_alloc(s, size, GENERIC_ALIGNMENT);
}


//
// Like linear_aligned_alloc, but zeros the memory. Just calls memset
// so this is purely convenience, unlike calloc.
//
MALLOC_FUNC
static
warn_unused
void*
linear_aligned_zalloc(LinearAllocator* restrict s, size_t size, size_t alignment){
    void* result = linear_aligned_alloc(s, size, alignment);
    memset(result, 0, size);
    return result;
}

//
// Like linear_alloc, but zeros the memory. Just calls memset
// so this is purely convenience, unlike calloc.
//
MALLOC_FUNC
static
warn_unused
void*
linear_zalloc(LinearAllocator* restrict s, size_t size){
    return linear_aligned_zalloc(s, size, _Alignof(void*));
}

//
// Frees the allocation. If this pointer + size is exactly data + cursor, we can
// decrement the cursor. Thus if you dealloc in a stack-like pattern, you can
// efficiently reuse memory. If it's not, then it just ignores it.
// Mostly here to satisfy the Allocator interface, but you can use this
// for temporary strings and such.
//
static
void
linear_free(LinearAllocator* la, const void*_Nullable data, size_t size){
    if(!data)
        return;
    assert(size);
    if(la->_cursor + (const char*)la->_data == (const char*)data + size){
        la->_cursor -= size;
    }
}

//
// If the pointer + orig size is equal to the allocators cursor, then perfectly
// reallocs in place with no copy. If not, then it just allocates a new buffer
// and copies the data over.
//
static inline
void*
linear_realloc(LinearAllocator* la, void*_Nullable data, size_t orig_size, size_t new_size){
    // only support growing
    assert(new_size > orig_size);
    if(!data){
        return linear_alloc(la, new_size);
    }
    assert(new_size);
    // check if we can extend in place.
    if(la->_cursor + (char*)la->_data == (char*)data+orig_size){
        la->_cursor += new_size - orig_size;
        if(la->_cursor > la->high_water){
            la->high_water = la->_cursor;
        }
        return (void*)data; // cast to shut up nullability
    }
    // just do a memcpy
    void* result = linear_alloc(la, new_size);
    memcpy(result, data, orig_size);
    return result;
}

//
// Turns a specific LinearAllocator into the erased Allocator. Take care
// that the LinearAllocator outlives the Allocator!
//
static inline
Allocator
allocator_from_la(LinearAllocator* la){
    return (Allocator){
        ._data = la,
        .type = ALLOCATOR_LINEAR,
    };
}
#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
