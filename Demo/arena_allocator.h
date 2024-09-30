//
// Copyright © 2021-2024, David Priver <david@davidpriver.com>
//
#ifndef ARENA_ALLOCATOR_H
#define ARENA_ALLOCATOR_H
// size_t
#include <stddef.h>
// malloc, free
#include <stdlib.h>
// memcpy, memset
#include <string.h>
#include <assert.h>

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
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

//
// Fairly basic arena allocator. If it can fit an allocation, it just bumps a
// pointer.  If it can't, it allocates a new block of memory and maintains the
// arenas as a linked list.
//
// If the allocation is bigger than would fit in an arena, it allocates it
// independently and maintains a linked list of these big allocations.
//
typedef struct ArenaAllocator ArenaAllocator;
struct ArenaAllocator {
    struct Arena*_Nullable arena;
    struct BigAllocation*_Nullable big_allocations;
};

//
// TODO: Currently we back the arena allocator with malloc, but we could just
// call the OS apis (mmap, VirtualAlloc, etc.) ourselves. Would need to see how
// slow they are compared to malloc, but we are allocating relatively large
// amounts anyway...

//
// Header for a large allocation. Used to maintain a linked list of allocations.
typedef struct BigAllocation BigAllocation;
struct BigAllocation {
    struct BigAllocation*_Nullable next;
    size_t size;
};

enum {ARENA_PAGE_SIZE=4096};

// Arenas are in 64 page chunks. This might be excessive, idk.
enum {ARENA_SIZE=ARENA_PAGE_SIZE*64};

// The actual amount of data available is smaller due to the arena's header.
enum {ARENA_BUFFER_SIZE = ARENA_SIZE-sizeof(void*)-sizeof(size_t)-sizeof(size_t)};

//
// An arena that is linearly allocated from. Maintains both the current
// allocation point and the previous one so that fast realloc can be
// implemented if reallocing the last allocation.
//
typedef struct Arena Arena;
struct Arena {
    struct Arena*_Nullable prev; // The previous, exhausted arena.
    size_t used; // How much of this arena has been used.
    size_t last; // Before the last allocation, how much had been used.
    char buff[ARENA_BUFFER_SIZE];
};


//
// Rounds up to the nearest power of 8.
//
force_inline
size_t
ArenaAllocator_round_size_up(size_t size){
    size_t remainder = size & 7;
    if(remainder)
        size += 8 - remainder;
    return size;
}

//
// Allocates an uninitialized chunk of memory from the arena.
//
static
void*
ArenaAllocator_alloc(ArenaAllocator* aa, size_t size){
    size = ArenaAllocator_round_size_up(size);
    if(size > ARENA_BUFFER_SIZE){
        BigAllocation* ba = malloc(sizeof(*ba)+size);
        ba->next = aa->big_allocations;
        ba->size = size;
        aa->big_allocations = ba;
        return ba+1;
    }
    if(!aa->arena){
        Arena* arena = malloc(sizeof(*arena));
        arena->prev = NULL;
        arena->used = 0;
        arena->last = 0;
        aa->arena = arena;
    }
    if(size > ARENA_BUFFER_SIZE - aa->arena->used){
        Arena* arena = malloc(sizeof(*arena));
        arena->prev = aa->arena;
        arena->used = size;
        arena->last = 0;
        aa->arena = arena;
        return arena->buff;
    }
    aa->arena->last = aa->arena->used;
    aa->arena->used += size;
    return aa->arena->buff + aa->arena->last;
}
//
// Allocates a zeroed chunk of memory from the arena.
//
static
void*
ArenaAllocator_zalloc(ArenaAllocator* aa, size_t size){
    size = ArenaAllocator_round_size_up(size);
    if(size > ARENA_SIZE/2){
        BigAllocation* ba = calloc(1, sizeof(*ba)+size);
        ba->next = aa->big_allocations;
        ba->size = size;
        aa->big_allocations = ba;
        return ba+1;
    }
    if(!aa->arena){
        Arena* arena = calloc(1, sizeof(*arena));
        arena->prev = NULL;
        arena->used = 0;
        arena->last = 0;
        aa->arena = arena;
    }
    if(size > ARENA_BUFFER_SIZE - aa->arena->used){
        Arena* arena = calloc(1, sizeof(*arena));
        arena->prev = aa->arena;
        arena->used = size;
        arena->last = 0;
        aa->arena = arena;
        return arena->buff;
    }
    aa->arena->last = aa->arena->used;
    aa->arena->used += size;
    void* result =  aa->arena->buff + aa->arena->last;
    memset(result, 0, size);
    return result;
}

//
// Reallocs an allocation from the arena, attempting to do it in place if it
// was the last allocation. If not, is forced to just alloc + memcpy.
//
static
void*_Nullable
ArenaAllocator_realloc(ArenaAllocator* aa, void*_Nullable ptr, size_t old_size, size_t new_size){
    if(!old_size || !ptr)
        return ArenaAllocator_alloc(aa, new_size);
    if(!new_size)
        return NULL;
    old_size = ArenaAllocator_round_size_up(old_size);
    new_size = ArenaAllocator_round_size_up(new_size);
    if(new_size > ARENA_BUFFER_SIZE){
        BigAllocation* ba = malloc(sizeof(*ba)+new_size);
        ba->next = aa->big_allocations;
        ba->size = new_size;
        aa->big_allocations = ba;
        void* result = ba+1;
        if(old_size < new_size)
            memcpy(result, ptr, old_size);
        else
            memcpy(result, ptr, new_size);
        return result;
    }
    if(old_size > ARENA_BUFFER_SIZE){
        void* result = ArenaAllocator_alloc(aa, new_size);
        if(old_size < new_size)
            memcpy(result, ptr, old_size);
        else
            memcpy(result, ptr, new_size);
        return result;
    }
    assert(aa->arena);
    if(aa->arena->last + aa->arena->buff == ptr){
        if(new_size <= ARENA_BUFFER_SIZE - aa->arena->last){
            aa->arena->used = aa->arena->last + new_size;
            return ptr;
        }
    }
    void* result = ArenaAllocator_alloc(aa, new_size);
    if(old_size < new_size)
        memcpy(result, ptr, old_size);
    else
        memcpy(result, ptr, new_size);
    return result;
}

static
void
ArenaAllocator_free(ArenaAllocator* aa, const void*_Nullable ptr, size_t size){
    (void)aa, (void)ptr, (void)size;
}
//
// Free all allocations from the arenas. Deallocs the arenas themselves and
// frees the big allocation linked list as well.
//
static
void
ArenaAllocator_free_all(ArenaAllocator*_Nullable aa){
    Arena* arena = aa->arena;
    while(arena){
        Arena* to_free = arena;
        arena = arena->prev;
        free(to_free);
    }
    BigAllocation* ba = aa->big_allocations;
    while(ba){
        BigAllocation* to_free = ba;
        ba = ba->next;
        free(to_free);
    }
    aa->arena = NULL;
    aa->big_allocations = NULL;
    return;
}

typedef struct ArenaAllocatorStats ArenaAllocatorStats;
struct ArenaAllocatorStats {
    size_t used, capacity, big_used, big_count, arena_count;
};

static inline
ArenaAllocatorStats
ArenaAllocator_stats(ArenaAllocator* aa){
    ArenaAllocatorStats result = {0};
    for(Arena* arena = aa->arena; arena; arena = arena->prev){
        result.used += arena->used;
        result.capacity += sizeof(arena->buff);
        result.arena_count++;
    }
    for(BigAllocation* ba = aa->big_allocations; ba; ba = ba->next){
        result.big_used += ba->size;
        result.big_count++;
    }
    return result;
}

//
// Free is not supported, but maybe we should do the linear allocator strategy?
// Currently, the type erased Allocator just ignores frees.
//

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
