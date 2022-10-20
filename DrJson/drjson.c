//
// Copyright © 2022, David Priver
//
#ifndef DRJSON_C
#define DRJSON_C
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#ifndef DRJSON_NO_STDIO
#include <stdio.h>
#include <stdarg.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

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
#include "bit_util.h"
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

typedef struct DrJsonObjectPair DrJsonObjectPair;
struct DrJsonObjectPair {
    DrJsonValue key;
    DrJsonValue value;
};

typedef struct DrJsonHashIndex DrJsonHashIndex;
struct DrJsonHashIndex {
    uint32_t hash;
    uint32_t index;
};

// check the size_for_object_of_length
_Static_assert(sizeof(DrJsonHashIndex) + sizeof(DrJsonObjectPair) == sizeof(void*)*5,"");
static inline
ssize_t
alloc_obj(DrJsonContext* ctx, size_t initial_length){
    if(ctx->objects.capacity <= ctx->objects.count){
        size_t new_cap = ctx->objects.capacity? ctx->objects.capacity*2:8;
        DrJsonObject* p = ctx->allocator.realloc(ctx->allocator.user_pointer, ctx->objects.data, sizeof(DrJsonObject)*ctx->objects.capacity, sizeof(DrJsonObject)*new_cap);
        if(!p)
            return -1;
        ctx->objects.data = p;
        ctx->objects.capacity = new_cap;
    }
    ssize_t result = ctx->objects.count++;
    ctx->objects.data[result] = (DrJsonObject){0};
    if(initial_length){
        void* p = ctx->allocator.alloc(ctx->allocator.user_pointer, drjson_size_for_object_of_length(initial_length));
        if(!p) {
            ctx->objects.count--;
            return -1;
        }
        ctx->objects.data[result].object_items = p;
        ctx->objects.data[result].capacity = initial_length;
    }
    return result;
}

static inline
ssize_t
alloc_array(DrJsonContext* ctx, size_t initial_length){
    if(ctx->arrays.capacity <= ctx->arrays.count){
        size_t new_cap = ctx->arrays.capacity? ctx->arrays.capacity*2:8;
        DrJsonArray* p = ctx->arrays.data? ctx->allocator.realloc(ctx->allocator.user_pointer, ctx->arrays.data, sizeof(DrJsonArray)*ctx->arrays.capacity, sizeof(DrJsonArray)*new_cap) : ctx->allocator.alloc(ctx->allocator.user_pointer, sizeof(DrJsonArray)*new_cap);
        if(!p)
            return -1;
        ctx->arrays.data = p;
        ctx->arrays.capacity = new_cap;
    }
    ssize_t result = ctx->arrays.count++;
    ctx->arrays.data[result] = (DrJsonArray){0};
    if(initial_length){
        void* p = ctx->allocator.alloc(ctx->allocator.user_pointer, sizeof(DrJsonValue)*initial_length);
        if(!p) {
            ctx->arrays.count--;
            return -1;
        }
        ctx->arrays.data[result].array_items = p;
        ctx->arrays.data[result].capacity = initial_length;
    }
    return result;
}



DRJSON_API
DrJsonValue
drjson_make_object(DrJsonContext* ctx, size_t initial_length){
    ssize_t idx = alloc_obj(ctx, initial_length);
    if(idx < 0) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
    return (DrJsonValue){.kind=DRJSON_OBJECT, .object_idx=idx};
}

static inline
DrJsonValue
drjson_make_obj_keys(DrJsonValue o){
    o.kind = DRJSON_OBJECT_KEYS;
    return o;
}

static inline
DrJsonValue
drjson_make_obj_values(DrJsonValue o){
    o.kind = DRJSON_OBJECT_VALUES;
    return o;
}

static inline
DrJsonValue
drjson_make_obj_items(DrJsonValue o){
    o.kind = DRJSON_OBJECT_ITEMS;
    return o;
}

#if 0
static inline
DrJsonValue
drjson_make_array_view(DrJsonValue o){
    o.kind = DRJSON_ARRAY_VIEW;
    return o;
}
#endif

DRJSON_API
DrJsonValue
drjson_make_array(DrJsonContext* ctx, size_t initial_length){
    ssize_t idx = alloc_array(ctx, initial_length);
    if(idx < 0) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
    return (DrJsonValue){.kind=DRJSON_ARRAY, .array_idx=idx};
}

MALLOC_FUNC
ALLOCATOR_SIZE(2)
static
void*
wrapped_malloc(void*_Null_unspecified _unused, size_t size){
    (void)_unused;
    return malloc(size);
}

ALLOCATOR_SIZE(4)
static
void*
wrapped_realloc(void*_Null_unspecified _unused, void*_Nullable data, size_t orig_size, size_t new_size){
    (void)_unused;
    (void)orig_size;
    void* result = realloc(data, new_size);
    return result;
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
force_inline
static inline
// __attribute__((__noinline__))
void
skip_whitespace(DrJsonParseContext* ctx){
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    strip:
    for(;cursor != end; cursor++){
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
static inline
_Bool
match(DrJsonParseContext* ctx, char c){
    if(ctx->cursor == ctx->end)
        return 0;
    if(*ctx->cursor != c)
        return 0;
    ctx->cursor++;
    return 1;
}

static inline
DrJsonValue
parse_string(DrJsonParseContext* ctx){
    skip_whitespace(ctx);
    if(ctx->cursor == ctx->end)
        return drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "eof when beginning parsing string");
    const char* string_start;
    const char* string_end;
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    if(likely(match(ctx, '"'))){
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
        return drjson_make_string_no_copy(string_start, string_end-string_start);
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
                    continue;
            }
        }
        after2:
        ctx->cursor = cursor;
        string_end = cursor;
        return drjson_make_string_no_copy(string_start, string_end-string_start);
    }
}


static inline
DrJsonValue
parse_object(DrJsonParseContext* ctx){
    if(unlikely(!match(ctx, '{'))) {
        return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Expected a '{' to begin an object");
    }
    DrJsonValue result = drjson_make_object(ctx->ctx, 0);
    DrJsonValue error = {0};
    skip_whitespace(ctx);
    while(!match(ctx, '}')){
        if(unlikely(ctx->cursor == ctx->end)){
            error = drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "Eof before closing '}'");
            goto cleanup;
        }
        skip_whitespace(ctx);
        DrJsonValue key = parse_string(ctx);
        if(unlikely(key.kind == DRJSON_ERROR)){
            error = key;
            goto cleanup;
        }
        DrJsonValue item = drjson_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_object_set_item_no_copy_key(ctx->ctx, result, key.string, key.slen, 0, item);
        if(unlikely(err)){
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate space for an item while setting member of an object");
            goto cleanup;
        }
        skip_whitespace(ctx);
    }
    return result;
    cleanup:
    return error;
}

static inline
DrJsonValue
parse_array(DrJsonParseContext* ctx){
    if(!match(ctx, '[')) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Expected a '[' to begin an array");
    DrJsonValue result = drjson_make_array(ctx->ctx, 0);
    DrJsonValue error = {0};
    skip_whitespace(ctx);
    while(!match(ctx, ']')){
        if(unlikely(ctx->cursor == ctx->end)){
            error = drjson_make_error(DRJSON_ERROR_UNEXPECTED_EOF, "Eof before closing ']'");
            goto cleanup;
        }
        DrJsonValue item = drjson_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_array_push_item(ctx->ctx, result, item);
        if(unlikely(err)){
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to push an item onto an array");
            goto cleanup;
        }
        skip_whitespace(ctx);
    }
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

static inline force_inline
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

DRJSON_API
DrJsonValue
drjson_parse(DrJsonParseContext* ctx){
    ctx->depth++;
    if(unlikely(ctx->depth > 100))
        return drjson_make_error(DRJSON_ERROR_TOO_DEEP, "Too many levels of nesting.");
    skip_whitespace(ctx);
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
drjson_parse_string(DrJsonContext* jctx, const char* text, size_t length, uint32_t flags){
    if(flags & DRJSON_PARSE_STRING_COPY){
        DrJsonStringNode* node = drjson_store_string_copy(jctx, text, length);
        if(!node) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
        text = node->data;
    }
    DrJsonParseContext ctx = {
        .ctx = jctx,
        .begin = text,
        .cursor = text,
        .end = text+length,
        .depth = 0,
    };
    return drjson_parse(&ctx);
}

DRJSON_API
DrJsonValue
drjson_parse_braceless_string(DrJsonContext* jctx, const char* text, size_t length, uint32_t flags){
    if(flags & DRJSON_PARSE_STRING_COPY){
        DrJsonStringNode* node = drjson_store_string_copy(jctx, text, length);
        if(!node) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "oom");
        text = node->data;
    }
    DrJsonParseContext ctx = {
        .ctx = jctx,
        .begin = text,
        .cursor = text,
        .end = text+length,
        .depth = 0,
    };
    return drjson_parse_braceless_object(&ctx);
}

DRJSON_API
DrJsonValue
drjson_parse_braceless_object(DrJsonParseContext* ctx){
    DrJsonValue result = drjson_make_object(ctx->ctx, 0);
    DrJsonValue error = {0};
    ctx->depth++;
    skip_whitespace(ctx);
    for(skip_whitespace(ctx); ctx->cursor != ctx->end; skip_whitespace(ctx)){
        DrJsonValue key = parse_string(ctx);
        if(unlikely(key.kind == DRJSON_ERROR)){
            error = key;
            goto cleanup;
        }
        DrJsonValue item = drjson_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_object_set_item_no_copy_key(ctx->ctx, result, key.string, key.slen, 0, item);
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
    DrJsonArray* array = &ctx->arrays.data[a.array_idx];
    if(array->capacity < array->count+1){
        const DrJsonAllocator* allocator = &ctx->allocator;
        size_t old_cap = array->capacity;
        enum {ARRAY_MAX = 0x1fffffff};
        size_t new_cap = old_cap?old_cap*2:4;
        if(new_cap > ARRAY_MAX) return 1;
        DrJsonValue* new_items = array->array_items?
            allocator->realloc(allocator->user_pointer, array->array_items, old_cap*sizeof(*new_items), new_cap*sizeof(*new_items))
            : allocator->alloc(allocator->user_pointer, new_cap*sizeof(*new_items));
        if(!new_items) return 1;
        array->array_items = new_items;
        array->capacity = new_cap;
    }
    array->array_items[array->count++] = item;
    return 0;
}

DRJSON_API
int // 0 on success
drjson_array_insert_item(const DrJsonContext* ctx, DrJsonValue a, size_t idx, DrJsonValue item){
    if(a.kind != DRJSON_ARRAY) return 1;
    DrJsonArray* array = &ctx->arrays.data[a.array_idx];
    if(idx >= array->count) return 1;
    if(array->capacity < array->count+1){
        const DrJsonAllocator* allocator = &ctx->allocator;
        size_t old_cap = array->capacity;
        enum {ARRAY_MAX = 0x1fffffff};
        size_t new_cap = old_cap?old_cap*2:4;
        if(new_cap > ARRAY_MAX) return 1;
        DrJsonValue* new_items = array->array_items?
            allocator->realloc(allocator->user_pointer, array->array_items, old_cap*sizeof(*new_items), new_cap*sizeof(*new_items))
            : allocator->alloc(allocator->user_pointer, new_cap*sizeof(*new_items));
        if(!new_items) return 1;
        array->array_items = new_items;
        array->capacity = new_cap;
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
    DrJsonArray* array = &ctx->arrays.data[a.array_idx];
    if(!array->count)
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "Array is empty");
    return array->array_items[--array->count];
}

DRJSON_API
DrJsonValue
drjson_array_del_item(const DrJsonContext* ctx, DrJsonValue a, size_t idx){
    if(a.kind != DRJSON_ARRAY) return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "Argument is not an array");
    DrJsonArray* array = &ctx->arrays.data[a.array_idx];
    if(!array->count)
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "Array is empty");
    if(idx >= array->count){
        return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "Index out of bounds.");
    }
    if(idx == array->count-1)
        return drjson_array_pop_item(ctx, a);
    size_t nmove = array->count - idx-1;
    DrJsonValue result= array->array_items[idx];
    drj_memmove(array->array_items+idx, array->array_items+idx+1, nmove*sizeof(*array->array_items));
    array->count--;
    return result;
}

DRJSON_API
uint32_t
drjson_object_key_hash(const char* key, size_t keylen){
    uint32_t h = hash_align1(key, keylen);
    if(!h) h = 1024;
    return h;
}

static inline
force_inline
uint32_t
object_key_hash(const char* key, size_t keylen){
    uint32_t h = hash_align1(key, keylen);
    if(!h) h = 1024;
    return h;
}


static inline
force_inline
int
drjson_object_set_item(DrJsonContext* ctx, DrJsonValue o, const char* key, size_t keylen, uint32_t hash, DrJsonValue item, _Bool copy){
    if(o.kind != DRJSON_OBJECT) return 1;
    DrJsonObject* object = &ctx->objects.data[o.object_idx];
    const DrJsonAllocator* allocator = &ctx->allocator;
    enum {KEY_MAX = 0x7fffffff};
    enum {OBJECT_MAX = 0x1fffffff};
    if(keylen > KEY_MAX) return 1;
    if(!hash) hash = object_key_hash(key, keylen);
    if(unlikely(object->count *2 >= object->capacity)){
        if(!object->capacity){
            size_t new_cap = 4;
            size_t size = drjson_size_for_object_of_length(new_cap);
            void* p = allocator->alloc(allocator->user_pointer, size);
            if(!p) return 1;
            drj_memset(p, 0, size);
            object->object_items = p;
            object->capacity = new_cap;
        }
        else {
            size_t old_cap = object->capacity;
            size_t new_cap = old_cap * 2;
            if(new_cap > OBJECT_MAX) return 1;
            size_t size = drjson_size_for_object_of_length(new_cap);
            void* p = allocator->alloc(allocator->user_pointer, size);
            drj_memset(p, 0, size);
            DrJsonHashIndex* oldhi = object->object_items;
            DrJsonObjectPair* oldpairs = (DrJsonObjectPair*)(((char*)object->object_items)+old_cap*sizeof(DrJsonHashIndex));
            DrJsonHashIndex* newhi = p;
            DrJsonObjectPair* newpairs = (DrJsonObjectPair*)(((char*)p)+new_cap*sizeof(DrJsonHashIndex));
            drj_memcpy(newpairs, oldpairs, object->count*sizeof(*oldpairs));
            for(size_t i = 0; i < old_cap; i++){
                DrJsonHashIndex hi = oldhi[i];
                if(!hi.hash) continue;
                uint32_t idx = fast_reduce32(hi.hash, new_cap);
                while(newhi[idx].hash){
                    idx++;
                    if(idx >= new_cap) idx = 0;
                }
                newhi[idx] = hi;
            }
            allocator->free(allocator->user_pointer, object->object_items, drjson_size_for_object_of_length(old_cap));
            object->object_items = p;
            object->capacity = new_cap;
        }
    }
    uint32_t capacity = object->capacity;
    uint32_t idx = fast_reduce32(hash, capacity);
    DrJsonHashIndex* his = object->object_items;
    DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
    for(;;){
        DrJsonHashIndex hi = his[idx];
        if(!hi.hash){
            if(copy){
                char* newkey = allocator->alloc(allocator->user_pointer, keylen);
                if(!newkey) return 1;
                drj_memcpy(newkey, key, keylen);
                key = newkey;
            }
            size_t pidx = object->count++;
            DrJsonObjectPair* o = &pairs[pidx];
            *o = (DrJsonObjectPair){
                .key = copy?drjson_make_string_copy(ctx, key, keylen):drjson_make_string_no_copy(key, keylen),
                .value=item,
            };
            his[idx].hash = hash;
            his[idx].index = pidx;
            return 0;
        }
        if(hi.hash == hash){
            DrJsonObjectPair* o = &pairs[hi.index];
            if(o->key.slen == keylen && memcmp(o->key.string, key, keylen) == 0){
                o->value = item;
                return 0;
            }
        }
        idx++;
        if(idx >= capacity)
            idx = 0;
    }
}
DRJSON_API
int // 0 on success
drjson_object_set_item_no_copy_key(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item){
    return drjson_object_set_item(ctx, object, key, keylen, hash, item, 0);
}
DRJSON_API
int // 0 on success
drjson_object_set_item_copy_key(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item){
    return drjson_object_set_item(ctx, object, key, keylen, hash, item, 1);
}
DRJSON_API
DrJsonValue
drjson_object_get_item(const DrJsonContext* ctx, DrJsonValue o, const char* key, size_t keylen, uint32_t hash){
    if(!hash) hash = object_key_hash(key, keylen);
    if(o.kind != DRJSON_OBJECT) return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "not an object");
    const DrJsonObject* object = &ctx->objects.data[o.object_idx];
    if(!object->capacity)
        return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "not an object");
    uint32_t capacity = object->capacity;
    uint32_t idx = fast_reduce32(hash, capacity);
    const DrJsonHashIndex* his = object->object_items;
    const DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
    for(;;){
        DrJsonHashIndex hi = his[idx];
        if(!hi.hash) return drjson_make_error(DRJSON_ERROR_MISSING_KEY, "key is not valid for object");
        if(hi.hash == hash){
            const DrJsonObjectPair* o = &pairs[hi.index];
            if(o->key.slen == keylen && memcmp(o->key.string, key, keylen) == 0){
                return o->value;
            }
        }
        idx++;
        if(idx >= object->capacity)
            idx = 0;
    }
}
DRJSON_API
DrJsonValue
drjson_object_keys(DrJsonValue o){
    if(o.kind != DRJSON_OBJECT)
        return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "call to keys for non-object");
    return drjson_make_obj_keys(o);
}

DRJSON_API
DrJsonValue
drjson_object_values(DrJsonValue o){
    if(o.kind != DRJSON_OBJECT)
        return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "call to values for non-object");
    return drjson_make_obj_values(o);

    // DrJsonObject* object = &ctx->objects.data[o.object_idx];
    // DrJsonValue result = drjson_make_array(ctx, object->count);
    // size_t capacity = object->capacity;
    // DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
    // for(size_t i = 0; i < object->count; i++){
        // drjson_array_push_item(ctx, result, pairs[i].value);
    // }
    // return result;
}

DRJSON_API
DrJsonValue
drjson_object_items(DrJsonValue o){
    if(o.kind != DRJSON_OBJECT)
        return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "call to items for non-object");
    return drjson_make_obj_items(o);
    // DrJsonObject* object = &ctx->objects.data[o.object_idx];
    // DrJsonValue result = drjson_make_array(ctx, object->count);

    // size_t capacity = object->capacity;
    // DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
    // for(size_t i = 0; i < object->count; i++){
        // drjson_array_push_item(ctx, result, pairs[i].key);
        // drjson_array_push_item(ctx, result, pairs[i].value);
    // }
    // return result;
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
DrJsonValue
drjson_query(const DrJsonContext* ctx, DrJsonValue v, const char* query, size_t length){
    enum {
        GETITEM,
        SUBSCRIPT,
        QUOTED_GETITEM,
        KEYS,
        VALUES,
        // GLOB,
    };
    size_t begin = 0;
    size_t i = 0;
    DrJsonValue result = drjson_make_error(DRJSON_ERROR_INVALID_ERROR, "whoops");
    DrJsonValue o = v;
    #define RETERROR(code, mess) do { \
        result = drjson_make_error(code, mess); \
        return result; \
    }while(0)
    if(i == length) RETERROR(DRJSON_ERROR_UNEXPECTED_EOF, "Query is 0 length");
    Ldispatch:
    for(;i != length; i++){
        char c = query[i];
        switch(c){
            case '.':
                i++;
                LHack:
                begin = i;
                if(i == length) RETERROR(DRJSON_ERROR_UNEXPECTED_EOF, "Empty query after a '.'");
                switch(query[i]){
                    case '"':
                        i++;
                        begin = i;
                        goto Lquoted_getitem;
                    case '#':
                    case '$':
                    case '@':
                        i++;
                        if(length - i >= sizeof("keys")-1 && memcmp(query+i, "keys", sizeof("keys")-1) == 0){
                            i += sizeof("keys")-1;
                            goto Lkeys;
                        }
                        if(length - i >= sizeof("values")-1 && memcmp(query+i, "values", sizeof("values")-1) == 0){
                            i += sizeof("values")-1;
                            goto Lvalues;
                        }
                        if(length - i >= sizeof("items")-1 && memcmp(query+i, "items", sizeof("items")-1) == 0){
                            i += sizeof("items")-1;
                            goto Litems;
                        }
                        if(length - i >= sizeof("length")-1 && memcmp(query+i, "length", sizeof("length")-1) == 0){
                            i += sizeof("length")-1;
                            goto Llength;
                        }
                        RETERROR(DRJSON_ERROR_INVALID_CHAR, "Unknown special key");
                    case CASE_0_9:
                    case CASE_a_z:
                    case CASE_A_Z:
                    case '_':
                        goto Lgetitem;
                    default:
                        RETERROR(DRJSON_ERROR_INVALID_CHAR, "Invalid character identifier");
                }
            case '[':
                i++;
                begin = i;
                goto Lsubscript;
            default:
                if(i == 0)
                    goto LHack;
                RETERROR(DRJSON_ERROR_INVALID_CHAR, "Queries must continue with '.', '['");
        }
    }
    goto Ldone;
    Lgetitem:
        for(;i!=length;i++){
            switch(query[i]){
                case '.':
                case '[':
                    goto Ldo_getitem;
                case CASE_a_z:
                case CASE_A_Z:
                case CASE_0_9:
                case '_':
                case '-':
                    continue;
                default:
                    RETERROR(DRJSON_ERROR_INVALID_CHAR, "Invalid character in identifier query");
            }
        }
        // fall-through
    Ldo_getitem:
        if(i == begin) RETERROR(DRJSON_ERROR_INVALID_CHAR, "0 length query after '.'");
        o = drjson_object_get_item(ctx, o, query+begin, i-begin, 0);
        if(o.kind == DRJSON_ERROR) RETERROR(DRJSON_ERROR_MISSING_KEY, "Key not found");
        goto Ldispatch;
    Lsubscript:
        for(;i!=length;i++){
            switch(query[i]){
                case '-':
                case CASE_0_9:
                    continue;
                case ']':
                    goto Ldo_subscript;
                default:
                    RETERROR(DRJSON_ERROR_MISSING_KEY, "Invalid subscript character (must be integer)");
            }
        }
        // Need to see a ']'
        RETERROR(DRJSON_ERROR_UNEXPECTED_EOF, "No ']' found to close a subscript");
    Ldo_subscript:
        {
            // lazy
            Int64Result pr = parse_int64(query+begin, i-begin);
            if(pr.errored){
                RETERROR(DRJSON_ERROR_INVALID_VALUE, "Unable to parse number for subscript");
            }
            int64_t index = pr.result;
            o = drjson_get_by_index(ctx, o, index);
            if(o.kind == DRJSON_ERROR) return o;
        }
        i++;
        goto Ldispatch;
    Lquoted_getitem:
        for(;i!=length;i++){
            if(query[i] == '"'){
                assert(i != begin);
                assert(i);
                size_t nbackslash = 0;
                for(size_t back = i-1; back != begin; back--){
                    if(query[back] != '\\') break;
                    nbackslash++;
                }
                if(nbackslash & 1) continue;
                o = drjson_object_get_item(ctx, o, query+begin, i-begin, 0);
                if(o.kind == DRJSON_ERROR) RETERROR(DRJSON_ERROR_MISSING_KEY, "Key not found");
                i++;
                goto Ldispatch;
            }
        }
        RETERROR(DRJSON_ERROR_UNEXPECTED_EOF, "Unterminated quoted query");
    Llength:;
        int64_t len = drjson_len(ctx, o);
        if(len < 0) RETERROR(DRJSON_ERROR_TYPE_ERROR, "Length applied to non-object, non-array, non-string");
        o = drjson_make_uint((uint64_t)len);
        goto Ldispatch;
    Lkeys:
        if(o.kind != DRJSON_OBJECT) RETERROR(DRJSON_ERROR_TYPE_ERROR, "@keys applied to non-object");
        o = drjson_make_obj_keys(o);
        goto Ldispatch;
    Lvalues:
        if(o.kind != DRJSON_OBJECT) RETERROR(DRJSON_ERROR_TYPE_ERROR, "Querying @values of non-object type");
        o = drjson_make_obj_values(o);
        goto Ldispatch;
    Litems:
        if(o.kind != DRJSON_OBJECT) RETERROR(DRJSON_ERROR_TYPE_ERROR, "Querying @items of non-object type");
        o = drjson_make_obj_items(o);
        goto Ldispatch;
    Ldone:
        return o;
    #undef RETERROR
}

DRJSON_API
int64_t
drjson_len(const DrJsonContext* ctx, DrJsonValue v){
    switch(v.kind){
        case DRJSON_ARRAY:
        case DRJSON_ARRAY_VIEW:{
            const DrJsonArray* array = &ctx->arrays.data[v.array_idx];
            return array->count;
        }
        case DRJSON_OBJECT:
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_VALUES:{
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            return object->count;
        }
        case DRJSON_OBJECT_ITEMS:{
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            return 2*object->count;
        }
        case DRJSON_STRING:
            return v.slen;
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
            const DrJsonArray* array = &ctx->arrays.data[v.array_idx];
            if(array->count <= index)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            return array->array_items[index];
        }
        case DRJSON_OBJECT_KEYS:{
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            if(object->count <= index)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            size_t capacity = object->capacity;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
            return pairs[index].key;
        }
        case DRJSON_OBJECT_VALUES:{
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            if(object->count <= index)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            size_t capacity = object->capacity;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
            return pairs[index].value;
        }
        case DRJSON_OBJECT_ITEMS:{
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            size_t pidx = index/2;
            if(object->count <= pidx)
                return drjson_make_error(DRJSON_ERROR_INDEX_ERROR, "out of bounds indexing");
            size_t capacity = object->capacity;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*capacity);
            if(index & 1)
                return pairs[pidx].value;
            else
                return pairs[pidx].key;
        }
        case DRJSON_STRING:
            return drjson_make_string_no_copy(v.string+index, 1);
        default:
            return drjson_make_error(DRJSON_ERROR_TYPE_ERROR, "object does not support indexing by integer");
    }
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

#ifndef _WIN32
static
int
wrapped_write(void* restrict ud, const void* restrict data, size_t length){
    int fd = (int)(intptr_t)ud;
    return write(fd, data, length) != length;
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
    if(flags & DRJSON_PRETTY_PRINT)
        drjson_pretty_print_value_inner(ctx, &buffer, v, indent);
    else
        drjson_print_value_inner(ctx, &buffer, v);
    if(flags & DRJSON_APPEND_NEWLINE)
        drjson_buff_putc(&buffer, '\n');
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
        case DRJSON_STRING:
            drjson_buff_putc(buffer, '"');
            drjson_buff_write(buffer, v.string, v.slen);
            drjson_buff_putc(buffer, '"');
            break;
        case DRJSON_ARRAY_VIEW:
        case DRJSON_ARRAY:{
            drjson_buff_putc(buffer, '[');
            const DrJsonArray* array = &ctx->arrays.data[v.array_idx];
            for(size_t i = 0; i < array->count; i++){
                drjson_print_value_inner(ctx, buffer, array->array_items[i]);
                if(i != array->count-1)
                    drjson_buff_putc(buffer, ',');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT:{
            drjson_buff_putc(buffer, '{');
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                assert(o->key.kind == DRJSON_STRING);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, o->key.string, o->key.slen);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ':');
                drjson_print_value_inner(ctx, buffer, o->value);
            }
            drjson_buff_putc(buffer, '}');
        }break;
        case DRJSON_OBJECT_KEYS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                assert(o->key.kind == DRJSON_STRING);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, o->key.string, o->key.slen);
                drjson_buff_putc(buffer, '"');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_VALUES:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
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
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                newlined = 1;
                assert(o->key.kind == DRJSON_STRING);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, o->key.string, o->key.slen);
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
        case DRJSON_STRING:
            drjson_buff_putc(buffer, '"');
            drjson_buff_write(buffer, v.string, v.slen);
            drjson_buff_putc(buffer, '"');
            break;
        case DRJSON_ARRAY_VIEW:
        case DRJSON_ARRAY:{
            drjson_buff_putc(buffer, '[');
            const DrJsonArray* array = &ctx->arrays.data[v.array_idx];
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
                if(i != array->count-1)
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
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');

                assert(o->key.kind == DRJSON_STRING);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, o->key.string, o->key.slen);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ':');
                drjson_buff_putc(buffer, ' ');
                drjson_pretty_print_value_inner(ctx, buffer, o->value, indent+2);
            }
            if(newlined) {
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, '}');
        }break;
        case DRJSON_OBJECT_KEYS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');

                assert(o->key.kind == DRJSON_STRING);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, o->key.string, o->key.slen);
                drjson_buff_putc(buffer, '"');
            }
            if(newlined) {
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_VALUES:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
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
            if(newlined) {
                drjson_buff_putc(buffer, '\n');
                for(int i = 0; i < indent; i++)
                    drjson_buff_putc(buffer, ' ');
            }
            drjson_buff_putc(buffer, ']');
        }break;
        case DRJSON_OBJECT_ITEMS:{
            drjson_buff_putc(buffer, '[');
            const DrJsonObject* object = &ctx->objects.data[v.object_idx];
            int newlined = 0;
            DrJsonObjectPair* pairs = (DrJsonObjectPair*)(((char*)object->object_items)+sizeof(DrJsonHashIndex)*object->capacity);
            for(size_t i = 0; i < object->count; i++){
                DrJsonObjectPair* o = &pairs[i];
                if(newlined)
                    drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, '\n');
                newlined = 1;
                for(int ind = 0; ind < indent+2; ind++)
                    drjson_buff_putc(buffer, ' ');

                assert(o->key.kind == DRJSON_STRING);
                drjson_buff_putc(buffer, '"');
                drjson_buff_write(buffer, o->key.string, o->key.slen);
                drjson_buff_putc(buffer, '"');
                drjson_buff_putc(buffer, ',');
                drjson_buff_putc(buffer, ' ');
                drjson_pretty_print_value_inner(ctx, buffer, o->value, indent+2);
            }
            if(newlined) {
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

DRJSON_API
int
drjson_escape_string(const DrJsonAllocator* restrict allocator, const char* restrict unescaped, size_t length, char *_Nullable restrict *_Nonnull restrict outstring, size_t* restrict outlength){
    if(!length) return 1;
    // reserve 2 characters of output for every 2 characters of input.
    // This is enough space for all common json strings (the bytes that must
    // be escaped are rare).
    // We fix that up if we actually hit them.
    size_t allocated_size = length * 2;
    char* s = allocator->alloc(allocator->user_pointer, allocated_size);
    if(!s) return 1;
    const char* const hex = "0123456789abcdef";
    size_t cursor = 0;
    for(size_t i = 0; i < length; i++){
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

#if 0
DRJSON_API
int
drjson_unescape_string(const DrJsonAllocator* restrict allocator, const char* restrict unescaped, size_t length, char*_Nullable restrict *_Nonnull restrict outstring, size_t* restrict outlength){
    if(!length) return 1;
    // TODO
    return 1;
}
#endif

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

#undef drjson_kind
#undef drjson_error_code
#undef drjson_error_mess
#undef drjson_slen

// getters for langs that don't support bitfields
DRJSON_API
int
drjson_kind(DrJsonValue v){
    return v.kind;
}
DRJSON_API
int
drjson_error_code(DrJsonValue v){
    return v.error_code;
}

DRJSON_API
size_t
drjson_slen(DrJsonValue v){
    return v.slen;
}

DRJSON_API
const char*
drjson_error_mess(DrJsonValue v){
    return v.err_mess;
}


#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#include "fpconv/src/fpconv.c"

#endif
