#ifndef DRJSON_TEST_ALLOCATOR_H
#define DRJSON_TEST_ALLOCATOR_H
#include "drjson.h"
// allocator that tracks allocations for testing purposes.
static DrJsonAllocator get_test_allocator(void);
// Verifies that all allocations have been deallocated.
static void assert_all_freed(void);
#endif
