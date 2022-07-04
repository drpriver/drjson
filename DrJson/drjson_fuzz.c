//
// Copyright © 2022, David Priver
//
#define DRJSON_API static inline
#include "drjson.c"

// Libfuzz calls this function to do its fuzzing.

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    DrJsonAllocator allocator = drjson_stdc_allocator();
    DrJsonValue result = drjson_parse_string(allocator, (const char*)data, size);
    if(result.kind != DRJSON_ERROR){
        drjson_slow_recursive_free_all(&allocator, result);
    }
    return 0;
}
