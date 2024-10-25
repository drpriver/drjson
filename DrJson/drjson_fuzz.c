//
// Copyright © 2022-2024, David Priver <david@davidpriver.com>
//
#define DRJSON_API static inline
#include "drjson.c"

// Libfuzz calls this function to do its fuzzing.

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
    DrJsonAllocator allocator = drjson_stdc_allocator();
    DrJsonContext* ctx = drjson_create_ctx(allocator);
    if(!ctx) return 1;
    DrJsonParseContext pctx = {
        .begin = (const char*)data,
        .end = (const char*)data + size,
        .cursor = (const char*)data,
        .ctx = ctx,
        .depth = 0,
    };
    DrJsonValue result = drjson_parse(&pctx, DRJSON_PARSE_FLAG_INTERN_OBJECTS);
    (void)result;
    drjson_gc(ctx, &result, 1);
    drjson_gc(ctx, &result, 0);
    drjson_ctx_free_all(ctx);
    return 0;
}
