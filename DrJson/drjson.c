#ifndef DRJSON_C
#define DRJSON_C
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

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
#include "hash_func.h"
#include "bit_util.h"
#define PARSE_NUMBER_PARSE_FLOATS 1
#include "parse_numbers.h"

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
            const char* p = memchr(cursor, '*', cursor-end);
            if(p && p != end && p+1 != end && p[1] == '/'){
                cursor = p+2;
                goto strip;
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
    if(*ctx->cursor != c)
        return 0;
    ctx->cursor++;
    return 1;
}

DRJSON_API
void
drjson_slow_recursive_free_all(const DrJsonAllocator* allocator, DrJsonValue value){
    if(!value.allocated) return;
    switch(value.kind){
    case DRJSON_NUMBER:
    case DRJSON_INTEGER:
    case DRJSON_UINTEGER:
    case DRJSON_NULL:
    case DRJSON_BOOL:
    case DRJSON_ERROR:
    case DRJSON_CAPSULE:
        assert(!value.allocated);
        // actually unreachable but whatever
        return;
    case DRJSON_BOXED:
        assert(!value.allocated);
        // So in principle I could allow allocated
        // boxed values but I haven't decided what that
        // means in regards to sharing. Currently they are
        // non-owning views of a value located somewhere else.
        return;
    case DRJSON_STRING:
        allocator->free(allocator->user_pointer, value.string, value.count);
        return;
    case DRJSON_ARRAY:
        for(size_t i = 0; i < value.count; i++)
            drjson_slow_recursive_free_all(allocator, value.array_items[i]);
        if(value.array_items)
            allocator->free(allocator->user_pointer, value.array_items, value.capacity*sizeof(value.array_items[0]));
        return;
    case DRJSON_OBJECT:
        for(size_t i = 0; i < value.capacity; i++){
            DrJsonObjectPair* it = &value.object_items[i];
            if(!it->key) continue;
            if(it->key_allocated)
                allocator->free(allocator->user_pointer, it->key, it->key_length);
            drjson_slow_recursive_free_all(allocator, it->value);
        }
        if(value.object_items)
            allocator->free(allocator->user_pointer, value.object_items, value.capacity*sizeof(value.object_items[0]));
        return;
    }
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
        ctx->error_message = "Expected a '{' to begin an object";
        return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Expected a '{' to begin an object");
    }
    DrJsonValue result = {.kind=DRJSON_OBJECT};
    DrJsonValue error = {0};
    ctx->depth++;
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
        int err = drjson_object_set_item_no_copy_key(&ctx->allocator, &result, key.string, key.count, 0, item);
        if(unlikely(err)){
            if(!ctx->allocator.free_all)
                drjson_slow_recursive_free_all(&ctx->allocator, item);
            ctx->error_message = "Failed to allocate space for an item while setting member of an object";
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocte space for an item while setting member of an object");
            goto cleanup;
        }
        skip_whitespace(ctx);
    }
    ctx->depth--;
    return result;
    cleanup:
    if(!ctx->allocator.free_all)
        drjson_slow_recursive_free_all(&ctx->allocator, result);
    return error;
}

static inline
DrJsonValue
parse_array(DrJsonParseContext* ctx){
    if(!match(ctx, '[')) return drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Expected a '[' to begin an array");
    DrJsonValue result = {.kind=DRJSON_ARRAY};
    DrJsonValue error = {0};
    ctx->depth++;
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
        int err = drjson_array_push_item(&ctx->allocator, &result, item);
        if(unlikely(err)){
            if(!ctx->allocator.free_all)
                drjson_slow_recursive_free_all(&ctx->allocator, item);
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to push an item onto an array");
            goto cleanup;
        }
        skip_whitespace(ctx);
    }
    ctx->depth--;
    return result;
    cleanup:
    if(!ctx->allocator.free_all)
        drjson_slow_recursive_free_all(&ctx->allocator, result);
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
#if 0
    if(length >= 3 && memcmp(ctx->cursor, "yes", 3) == 0){
        ctx->cursor += 3;
        return drjson_make_bool(1);
    }
    if(length >= 2 && memcmp(ctx->cursor, "no", 2) == 0){
        ctx->cursor += 2;
        return drjson_make_bool(0);
    }
#endif
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
            ctx->error_message = "Character is not a valid starting character for json.";
            result = drjson_make_error(DRJSON_ERROR_INVALID_CHAR, "Character is not a valid starting character for json");
            break;
    }
    ctx->depth--;
    return result;
}
DRJSON_API
DrJsonValue
drjson_parse_braceless_object(DrJsonParseContext* ctx){
    DrJsonValue result = {.kind=DRJSON_OBJECT};
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
        int err = drjson_object_set_item_no_copy_key(&ctx->allocator, &result, key.string, key.count, 0, item);
        if(unlikely(err)){
            if(!ctx->allocator.free_all)
                drjson_slow_recursive_free_all(&ctx->allocator, item);
            ctx->error_message = "Failed to allocate space for an item while setting member of an object";
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocte space for an item while setting member of an object");
            goto cleanup;
        }
    }
    ctx->depth--;
    return result;
    cleanup:
    if(!ctx->allocator.free_all)
        drjson_slow_recursive_free_all(&ctx->allocator, result);
    return error;
}

DRJSON_API
int // 0 on success
drjson_array_push_item(const DrJsonAllocator* allocator, DrJsonValue* array, DrJsonValue item){
    if(array->kind != DRJSON_ARRAY) return 1;
    if(array->capacity < array->count+1){
        if(array->capacity && !array->allocated){ // We don't own this buffer
            return 1;
        }
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
        array->allocated = 1;
    }
    array->array_items[array->count++] = item;
    return 0;
}

DRJSON_API
force_inline
uint32_t
drjson_object_key_hash(const char* key, size_t keylen){
    return hash_align1(key, keylen);
}


static inline
force_inline
int
drjson_object_set_item(const DrJsonAllocator* allocator, DrJsonValue* object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item, _Bool copy){
    if(object->kind != DRJSON_OBJECT) return 1;
    enum {KEY_MAX = 0x7fffffff};
    enum {OBJECT_MAX = 0x1fffffff};
    if(keylen > KEY_MAX) return 1;
    if(!hash) hash = drjson_object_key_hash(key, keylen);
    if(unlikely(object->count *2 >= object->capacity)){
        if(!object->capacity){
            size_t new_cap = 4;
            DrJsonObjectPair* p = allocator->alloc(allocator->user_pointer, new_cap*sizeof(*p));
            if(!p) return 1;
            __builtin_memset(p, 0, new_cap*sizeof(*p));
            object->object_items = p;
            object->allocated = 1;
            object->capacity = new_cap;
        }
        else {
            if(unlikely(!object->allocated)) return 1;
            size_t old_cap = object->capacity;
            size_t new_cap = old_cap * 2;
            if(new_cap > OBJECT_MAX) return 1;
            DrJsonObjectPair* p = allocator->alloc(allocator->user_pointer, new_cap*sizeof(*p));
            __builtin_memset(p, 0, new_cap*sizeof(*p));
            for(size_t i = 0; i < old_cap; i++){
                DrJsonObjectPair o = object->object_items[i];
                if(!o.key) continue;
                size_t idx = o.key_hash % new_cap;
                while(p[idx].key){
                    idx++;
                    if(idx >= new_cap) idx = 0;
                }
                p[idx] = o;
            }
            allocator->free(allocator->user_pointer, object->object_items, old_cap*sizeof(*object->object_items));
            object->object_items = p;
            object->capacity = new_cap;
        }
    }
    size_t cap = object->capacity;
    size_t idx = hash % cap;
    for(;;){
        DrJsonObjectPair* o = &object->object_items[idx];
        if(!o->key){
            if(copy){
                char* newkey = allocator->alloc(allocator->user_pointer, keylen);
                if(!newkey) return 1;
                __builtin_memcpy(newkey, key, keylen);
                key = newkey;
            }
            *o = (DrJsonObjectPair){.key=key, .key_length=keylen, .key_hash=hash, .value=item, .key_allocated=copy};
            object->count++;
            return 0;
        }
        if(o->key_length == keylen && o->key_hash == hash && memcmp(o->key, key, keylen) == 0){
            o->value = item;
            return 0;
        }
        idx++;
        if(idx >= cap)
            idx = 0;
    }
}
DRJSON_API
int // 0 on success
drjson_object_set_item_no_copy_key(const DrJsonAllocator* allocator, DrJsonValue* object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item){
    return drjson_object_set_item(allocator, object, key, keylen, hash, item, 0);
}
DRJSON_API
int // 0 on success
drjson_object_set_item_copy_key(const DrJsonAllocator* allocator, DrJsonValue* object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item){
    return drjson_object_set_item(allocator, object, key, keylen, hash, item, 1);
}
DRJSON_API
DrJsonValue*_Nullable
drjson_object_get_item(DrJsonValue object, const char* key, size_t keylen, uint32_t hash){
    if(!hash) hash = drjson_object_key_hash(key, keylen);
    if(object.kind != DRJSON_OBJECT) return NULL;
    if(!object.capacity)
        return NULL;
    size_t idx = hash % object.capacity;
    for(;;){
        DrJsonObjectPair* o = &object.object_items[idx];
        if(!o->key){
            return NULL;
        }
        if(o->key_length == keylen && o->key_hash == hash && memcmp(o->key, key, keylen) == 0){
            return &o->value;
        }
        idx++;
        if(idx >= object.capacity)
            idx = 0;
    }
}

DRJSON_API
DrJsonValue
drjson_query(DrJsonValue* v, const char* query, size_t length){
    DrJsonValue result = drjson_multi_query(NULL, v, query, length);
    return result;
}

DRJSON_API
DrJsonValue
drjson_checked_query(DrJsonValue* v, int type, const char* query, size_t length){
    DrJsonValue o = drjson_query(v, query, length);
    if(o.kind == DRJSON_ERROR) return o;
    assert(o.kind == DRJSON_BOXED || o.kind == DRJSON_UINTEGER);
    if(o.kind == DRJSON_UINTEGER && type == DRJSON_UINTEGER)
        return o;
    if(o.kind == DRJSON_BOXED && o.boxed->kind == type)
        return o;
    return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Wrong type");
}

static inline
DrJsonValue*
debox(DrJsonValue* v){
    while(v->kind == DRJSON_BOXED)
        v = v->boxed;
    return v;
}


DRJSON_API
DrJsonValue
drjson_multi_query(const DrJsonAllocator*_Nullable allocator, DrJsonValue* v, const char* query, size_t length){
    v = debox(v);
    enum {
        GETITEM,
        SUBSCRIPT,
        QUOTED_GETITEM,
        KEYS,
        VALUES,
        // GLOB,
    };
    int err;
    size_t begin = 0;
    size_t i = 0;
    DrJsonValue result = drjson_make_error(DRJSON_ERROR_INVALID_ERROR, "whoops");
    DrJsonValue* o = v;
    #define ERROR(code, mess) do { \
        if(result.allocated) \
            drjson_slow_recursive_free_all(allocator, result); \
        result = drjson_make_error(code, mess); \
        return result; \
    }while(0)
    if(i == length) ERROR(DRJSON_ERROR_UNEXPECTED_EOF, "Query is 0 length");
    Ldispatch:
    o = debox(o);
    for(;i != length; i++){
        char c = query[i];
        switch(c){
            case '.':
                i++;
                LHack:
                begin = i;
                if(i == length) ERROR(DRJSON_ERROR_UNEXPECTED_EOF, "Empty query after a '.'");
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
                            if(i != length) ERROR(DRJSON_ERROR_INVALID_CHAR, "More query after @keys is unsupported");
                            goto Lkeys;
                        }
                        if(length - i >= sizeof("values")-1 && memcmp(query+i, "values", sizeof("values")-1) == 0){
                            i += sizeof("values")-1;
                            if(i != length) ERROR(DRJSON_ERROR_INVALID_CHAR, "More query after @values is unsupported");
                            goto Lvalues;
                        }
                        if(length - i >= sizeof("length")-1 && memcmp(query+i, "length", sizeof("length")-1) == 0){
                            i += sizeof("length")-1;
                            if(i != length) ERROR(DRJSON_ERROR_INVALID_CHAR, "More query after @length is unsupported");

                            goto Llength;
                        }
                        ERROR(DRJSON_ERROR_INVALID_CHAR, "Unknown special key");
                    case CASE_0_9:
                    case CASE_a_z:
                    case CASE_A_Z:
                    case '_':
                        goto Lgetitem;
                    default:
                        ERROR(DRJSON_ERROR_INVALID_CHAR, "Invalid character identifier");
                }
            case '[':
                i++;
                begin = i;
                goto Lsubscript;
            default:
                if(i == 0)
                    goto LHack;
                ERROR(DRJSON_ERROR_INVALID_CHAR, "Queries must continue with '.', '['");
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
                    ERROR(DRJSON_ERROR_INVALID_CHAR, "Invalid character in identifier query");
            }
        }
        // fall-through
    Ldo_getitem:
        if(i == begin) ERROR(DRJSON_ERROR_INVALID_CHAR, "0 length query after '.'");
        o = drjson_object_get_item(*o, query+begin, i-begin, 0);
        if(!o) ERROR(DRJSON_ERROR_MISSING_KEY, "Key not found");
        goto Ldispatch;
    Lsubscript:
        for(;i!=length;i++){
            switch(query[i]){
                case CASE_0_9:
                    continue;
                case ']':
                    goto Ldo_subscript;
                default:
                    ERROR(DRJSON_ERROR_MISSING_KEY, "Invalid subscript character (must be integer)");
            }
        }
        // Need to see a ']'
        ERROR(DRJSON_ERROR_UNEXPECTED_EOF, "No ']' found to close a subscript");
    Ldo_subscript:
        {
            // lazy
            Uint64Result pr = parse_unsigned_human(query+begin, i-begin);
            if(pr.errored){
                ERROR(DRJSON_ERROR_INVALID_VALUE, "Unable to parse number for subscript");
            }
            uint64_t index = pr.result;
            if(o->kind != DRJSON_ARRAY) ERROR(DRJSON_ERROR_MISSING_KEY, "Subscript applied to non-array");
            if(index < 0 || index >= o->count) ERROR(DRJSON_ERROR_MISSING_KEY, "Subscript out of bounds of array");
            o = &o->array_items[index];
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
                o = drjson_object_get_item(*o, query+begin, i-begin, 0);
                if(!o) ERROR(DRJSON_ERROR_MISSING_KEY, "Key not found");
                i++;
                goto Ldispatch;
            }
        }
        ERROR(DRJSON_ERROR_UNEXPECTED_EOF, "Unterminated quoted query");
    Llength:
        if(o->kind != DRJSON_OBJECT && o->kind != DRJSON_ARRAY && o->kind != DRJSON_STRING)
            ERROR(DRJSON_ERROR_INDEX_ERROR, "Length applied to non-object, non-array, non-string");
        if(i != length) ERROR(DRJSON_ERROR_INVALID_CHAR, "Queries after @length not supported");
        if(!allocator) ERROR(DRJSON_ERROR_ALLOC_FAILURE, "NULL allocator passed for result that needs allocation");
        return drjson_make_uint(o->count);
    Lkeys:
        if(o->kind != DRJSON_OBJECT) ERROR(DRJSON_ERROR_MISSING_KEY, "@keys applied to non-object");
        if(i != length) ERROR(DRJSON_ERROR_INVALID_CHAR, "Queries after @length not supported");
        for(size_t i = 0; i < o->capacity; i++){
            DrJsonObjectPair* p = &o->object_items[i];
            if(!p->key) continue;
            DrJsonValue v = drjson_make_string_no_copy(p->key, p->key_length);
            if(!allocator) ERROR(DRJSON_ERROR_ALLOC_FAILURE, "NULL allocator passed for result that needs allocation");
            if(result.kind != DRJSON_ARRAY) result = drjson_make_array(allocator, o->count);
            err = drjson_array_push_item(allocator, &result, v);
            if(err) ERROR(DRJSON_ERROR_ALLOC_FAILURE, "Failed to push to the result array");
        }
        return result;
    Lvalues:
        if(o->kind != DRJSON_OBJECT) ERROR(DRJSON_ERROR_MISSING_KEY, "Querying @values of non-object type");
        if(i != length) ERROR(DRJSON_ERROR_INVALID_CHAR, "Queries after @values not supported");
        for(size_t i = 0; i < o->capacity; i++){
            DrJsonObjectPair* p = &o->object_items[i];
            if(!p->key) continue;
            DrJsonValue v = drjson_make_box(&p->value);
            if(!allocator) ERROR(DRJSON_ERROR_ALLOC_FAILURE, "NULL allocator passed for result that needs allocation");
            if(result.kind != DRJSON_ARRAY) result = drjson_make_array(allocator, o->count);
            err = drjson_array_push_item(allocator, &result, v);
            if(err) ERROR(DRJSON_ERROR_ALLOC_FAILURE, "Failed to push to the result array");
        }
        return result;
    Ldone:
        return drjson_make_box(o);
    #undef ERROR
}

#ifndef DRJSON_NO_STDIO

DRJSON_API
int
drjson_print_value(FILE* fp, DrJsonValue v, int indent, unsigned flags){
    int result = 0;
    int pretty = flags & DRJSON_PRETTY_PRINT;
    switch(v.kind){
        case DRJSON_NUMBER:
            // This sucks. We need a better float formatting lib
            result = fprintf(fp, "%.12g", v.number); break;
        case DRJSON_INTEGER:
            result = fprintf(fp, "%lld", v.integer); break;
        case DRJSON_UINTEGER:
            result = fprintf(fp, 1?"%llu":"%#llx", v.uinteger); break;
        case DRJSON_STRING:
            result = fprintf(fp, "\"%.*s\"", (int)v.count, v.string); break;
        case DRJSON_ARRAY:{
            result = fputc('[', fp);
            if(pretty && v.count)
                result = fputc('\n', fp);
            for(size_t i = 0; i < v.count; i++){
                if(pretty)
                    for(int i = 0; i < indent+2; i++)
                        result = fputc(' ', fp);
                result = drjson_print_value(fp, v.array_items[i], indent+2, flags);
                if(i != v.count-1)
                    result = fputc(',', fp);
                if(pretty)
                    result = fputc('\n', fp);
            }
            if(pretty && v.count){
                for(int i = 0; i < indent; i++)
                    result = fputc(' ', fp);
            }
            result = fputc(']', fp);
        }break;
        case DRJSON_OBJECT:{
            result = fputc('{', fp);
            int newlined = 0;
            for(size_t i = 0; i < v.capacity; i++){
                DrJsonObjectPair* o = &v.object_items[i];
                if(!o->key) continue;
                if(newlined)
                    result = fputc(',', fp);
                if(pretty)
                    result = fputc('\n', fp);
                newlined = 1;
                if(pretty)
                    for(int ind = 0; ind < indent+2; ind++)
                        result = fputc(' ', fp);
                result = fprintf(fp, "\"%.*s\":", (int)o->key_length, o->key);
                if(pretty) result = fputc(' ', fp);
                result = drjson_print_value(fp, o->value, indent+2, flags);

            }
            if(pretty && newlined) {
                result = fputc('\n', fp);
                for(int i = 0; i < indent; i++)
                    result = fputc(' ', fp);
            }
            result = fputc('}', fp);
        }break;
        case DRJSON_NULL:
            result = fprintf(fp, "null"); break;
        case DRJSON_BOOL:
            if(v.boolean)
                result = fprintf(fp, "true");
            else
                result = fprintf(fp, "false");
            break;
        case DRJSON_CAPSULE:
            result = fprintf(fp, "(capsule) %p", v.capsule);
            break;
        case DRJSON_BOXED:
            // fputc('*', fp);
            result = drjson_print_value(fp, *v.boxed, indent, flags);
            break;
        case DRJSON_ERROR:
            result = fprintf(fp, "Error: %s (Code %d): %s", drjson_get_error_name(v), drjson_get_error_code(v), v.err_mess);
            break;
    }
    return result < 0 ? result: 0;
}
#endif



#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
