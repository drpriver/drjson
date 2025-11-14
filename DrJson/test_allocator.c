#ifndef DRJSON_TEST_ALLOCATOR_C
#define DRJSON_TEST_ALLOCATOR_C
#include <stdlib.h>
#include <stdio.h>
#include "test_allocator.h"
#include "hash_func.h"
#include "../debugging.h"
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif
typedef struct Allocation Allocation;
struct Allocation {
    const void* ptr;
    size_t sz;
    _Bool freed;
    BacktraceArray* alloc_trace;
    BacktraceArray*_Null_unspecified free_trace;
};

static
void
dump_a(const Allocation* a){
    fprintf(stderr, "Alloced at\n");
    dump_bt(a->alloc_trace);
    fprintf(stderr, "\n");
    if(a->free_trace){
        fprintf(stderr, "Freed at\n");
        dump_bt(a->free_trace);
        fprintf(stderr, "\n");
    }
}

enum {TEST_ALLOCATOR_CAP = 256*256*2};
typedef struct TestAllocator TestAllocator;
struct TestAllocator {
    size_t cursor;
    uint32_t idxes[TEST_ALLOCATOR_CAP*2];
    Allocation allocations[TEST_ALLOCATOR_CAP];
};

static TestAllocator test_allocator;

static inline
uint32_t
hash_ptr(const void* ptr){
    uintptr_t data[1] = {(uintptr_t)ptr};
    uint32_t hash = hash_align8(data, sizeof data);
    return hash;
}

static
Allocation*
test_getsert(TestAllocator* ta, const void* ptr){
    uint32_t hash = hash_ptr(ptr);
    uint32_t idx = fast_reduce32(hash, TEST_ALLOCATOR_CAP*2);
    for(;;){
        uint32_t i = ta->idxes[idx];
        if(i == 0){ // unset
            assert(ta->cursor < TEST_ALLOCATOR_CAP);
            Allocation* a = &ta->allocations[ta->cursor++];
            ta->idxes[idx] = (uint32_t)ta->cursor; // index of slot +1
            *a = (Allocation){
                .ptr = ptr,
            };
            return a;
        }
        Allocation* a = &ta->allocations[i-1];
        if(a->ptr == ptr){
            return a;
        }
        idx++;
        if(idx >= TEST_ALLOCATOR_CAP*2) idx = 0;
    }
}

static
Allocation*_Nullable
test_get(TestAllocator* ta, const void* ptr){
    uint32_t hash = hash_ptr(ptr);
    uint32_t idx = fast_reduce32(hash, TEST_ALLOCATOR_CAP*2);
    for(;;){
        uint32_t i = ta->idxes[idx];
        if(i == 0){ // unset
            return NULL;
        }
        Allocation* a = &ta->allocations[i-1];
        if(a->ptr == ptr){
            return a;
        }
        idx++;
        if(idx >= TEST_ALLOCATOR_CAP*2) idx = 0;
    }
}

static
void* _Nullable
test_alloc(void* up, size_t size){
    TestAllocator* ta = up;
    void* p = malloc(size);
    if(!p) return NULL;
    assert(ta->cursor < 256*256*2);
    Allocation* a = test_getsert(ta, p);
    a->sz = size;
    if(a->free_trace) free_bt(a->free_trace);
    a->free_trace = NULL;
    a->freed = 0;
    a->alloc_trace = get_bt();
    return p;
}

static
void
record_free(void* up, const void*_Null_unspecified ptr, size_t size){
    if(!ptr) return;
    TestAllocator* ta = up;
    Allocation* a = test_get(ta, ptr);
    if(!a){
        bt();
        assert(!"freeing wild pointer");
    }
    if(a->sz != size){
        dump_a(a);
        fprintf(stderr, "Freed at\n");
        bt();
        assert(!"Freeing with wrong size");
    }
    if(a->freed){
        dump_a(a);
        fprintf(stderr, "Freed again at\n");
        bt();
        fprintf(stderr, "\n");
        assert(!"Double free");
    }
    a->freed = 1;
    a->free_trace = get_bt();
}

static
void
test_free(void* up, const void*_Null_unspecified ptr, size_t size){
    record_free(up, ptr, size);
    free((void*)ptr);
}

static
void*_Nullable
test_realloc(void* up, void*_Nullable ptr, size_t old_sz, size_t new_sz){
    TestAllocator* ta = up;
    if(!ptr){
        assert(!old_sz);
        return test_alloc(up, new_sz);
    }
    assert(old_sz);
    if(!new_sz){
        test_free(up, ptr, old_sz);
        return NULL;
    }
    record_free(ta, ptr, old_sz);
    void* p = test_alloc(ta, new_sz);
    if(!p) return NULL;
    if(old_sz < new_sz)
        drj_memcpy(p, ptr, old_sz);
    else
        drj_memcpy(p, ptr, new_sz);
    free(ptr);

    Allocation* a = test_getsert(ta, p);
    a->sz = new_sz;
    assert(!a->freed);
    return p;
}


static
void
assert_all_freed(void){
    TestAllocator* ta = &test_allocator;
    for(size_t i = 0; i < ta->cursor; i++){
        Allocation * a = &ta->allocations[i];
        if(!a->freed){
            dump_a(a);
        }
        assert(ta->allocations[i].freed);
    }
}

static
DrJsonAllocator
get_test_allocator(void){
    assert_all_freed();
    TestAllocator* ta = &test_allocator;
    for(size_t i = 0; i < ta->cursor; i++){
        Allocation * a = &ta->allocations[i];
        if(a->alloc_trace) free_bt(a->alloc_trace);
        if(a->free_trace) free_bt(a->free_trace);
    }
    memset(&test_allocator, 0, sizeof test_allocator);
    return (DrJsonAllocator){
        .user_pointer = &test_allocator,
        .alloc = test_alloc,
        .realloc = test_realloc,
        .free = test_free,
    };
}
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
