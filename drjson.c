#ifndef DRJSON_C
#define DRJSON_C
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "drjson.h"
#include "hash_func.h"
#include "bit_util.h"

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
DRJsonAllocator
drjson_stdc_allocator(void){
    return (DRJsonAllocator){
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
skip_whitespace(DRJsonParseContext* ctx){
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    for(;cursor != end; cursor++){
        switch(*cursor){
            case ' ': case '\r': 
            case '\t': case '\n': 
            case ',': case ':':
                continue;
            default:
                goto end;
        }
    }
    end:
    ctx->cursor = cursor;
}

force_inline
static inline
_Bool
match(DRJsonParseContext* ctx, char c){
    if(*ctx->cursor != c)
        return 0;
    ctx->cursor++;
    return 1;
}

DRJSON_API
void
drjson_slow_recursive_free_all(const DRJsonAllocator* allocator, DRJsonValue value){
    if(!value.allocated) return;
    switch(value.kind){
    case DRJSON_NUMBER:
    case DRJSON_INTEGER:
    case DRJSON_UINTEGER:
    case DRJSON_NULL:
    case DRJSON_BOOL:
    case DRJSON_ERROR:
        // actually unreachable but whatever
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
            DRJsonObjectPair* it = &value.object_items[i];
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
DRJsonValue
parse_string(DRJsonParseContext* ctx){
    skip_whitespace(ctx);
    if(ctx->cursor == ctx->end)
        return drjson_make_error(9000);
    const char* string_start;
    const char* string_end;
    const char* cursor = ctx->cursor;
    const char* end = ctx->end;
    if(likely(match(ctx, '"'))){
        cursor = ctx->cursor;
        string_start = cursor;
        for(;;){
            const char* close = memchr(cursor, '"', end-cursor);
            if(unlikely(!close)) return drjson_make_error(9999);
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
                    if(cursor == string_start) return drjson_make_error(899);
                    else goto after2;
                case CASE_a_z:
                case CASE_A_Z:
                case CASE_0_9:
                case '_':
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
DRJsonValue
parse_object(DRJsonParseContext* ctx){
    if(unlikely(!match(ctx, '{'))) {
        ctx->error_message = "Expected a '{' to begin an object";
        return drjson_make_error(DRJSON_ERROR_INVALID_START_CHAR);
    }
    DRJsonValue result = {.kind=DRJSON_OBJECT};
    DRJsonValue error = {0};
    ctx->depth++;
    skip_whitespace(ctx);
    while(!match(ctx, '}')){
        if(unlikely(ctx->cursor == ctx->end)){
            error = drjson_make_error(9000);
            goto cleanup;
        }
        skip_whitespace(ctx);
        DRJsonValue key = parse_string(ctx);
        if(unlikely(key.kind == DRJSON_ERROR)){
            error = key;
            goto cleanup;
        }
        DRJsonValue item = drjson_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_object_set_item_no_copy_key(&ctx->allocator, &result, key.string, key.count, 0, item);
        if(unlikely(err)){
            if(!ctx->allocator.free_all)
                drjson_slow_recursive_free_all(&ctx->allocator, item);
            ctx->error_message = "Failed to allocate space for an item while setting member of an object";
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE);
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
DRJsonValue
parse_array(DRJsonParseContext* ctx){
    if(!match(ctx, '[')) return drjson_make_error(DRJSON_ERROR_INVALID_START_CHAR);
    DRJsonValue result = {.kind=DRJSON_ARRAY};
    DRJsonValue error = {0};
    ctx->depth++;
    skip_whitespace(ctx);
    while(!match(ctx, ']')){
        if(unlikely(ctx->cursor == ctx->end)){
            error = drjson_make_error(9000);
            goto cleanup;
        }
        DRJsonValue item = drjson_parse(ctx);
        if(unlikely(item.kind == DRJSON_ERROR)){
            error = item;
            goto cleanup;
        }
        int err = drjson_array_push_item(&ctx->allocator, &result, item);
        if(unlikely(err)){
            if(!ctx->allocator.free_all)
                drjson_slow_recursive_free_all(&ctx->allocator, item);
            error = drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE);
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
DRJsonValue
parse_bool_null(DRJsonParseContext* ctx){
    skip_whitespace(ctx);
    ptrdiff_t length = ctx->end - ctx->cursor;
    if(length >= 4 && memcmp(ctx->cursor, "true", 4) == 0){
        ctx->cursor += 4;
        return drjson_make_bool(1);
    }
    if(length >= 5 && memcmp(ctx->cursor, "false", 5) == 0){
        ctx->cursor += 5;
        return drjson_make_bool(0);
    }
    if(length >= 3 && memcmp(ctx->cursor, "yes", 3) == 0){
        ctx->cursor += 3;
        return drjson_make_bool(1);
    }
    if(length >= 2 && memcmp(ctx->cursor, "no", 2) == 0){
        ctx->cursor += 2;
        return drjson_make_bool(0);
    }
    if(length >= 4 && memcmp(ctx->cursor, "null", 4) == 0){
        ctx->cursor += 4;
        return drjson_make_null();
    }
    return drjson_make_error(DRJSON_ERROR_INVALID_START_CHAR);
}
static inline
DRJsonValue
parse_number(DRJsonParseContext* ctx){
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
    if(!length) return drjson_make_error(876);
    ctx->cursor = cursor;
    if(has_exponent || has_decimal)
        return drjson_make_number(strtod(num_begin, NULL));
    else if(has_minus)
        return drjson_make_int(strtoll(num_begin, NULL, 10));
    else
        return drjson_make_uint(strtoull(num_begin, NULL, 10));
}

DRJSON_API
DRJsonValue
drjson_parse(DRJsonParseContext* ctx){
    ctx->depth++;
    skip_whitespace(ctx);
    DRJsonValue result;
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
        case 'y':
        case 't':
        case 'f':
        case 'n':
            result = parse_bool_null(ctx);
            break;
        case '+':
        case '.': case '-': case '0': case '1': case '2': case '3': 
        case '4': case '5': case '6': case '7': case '8': case '9':
            result = parse_number(ctx);
            break;
        default:
            ctx->error_message = "Character is not a valid starting character for json.";
            result = drjson_make_error(DRJSON_ERROR_INVALID_START_CHAR);
            break;
    }
    ctx->depth--;
    return result;
}

DRJSON_API 
int // 0 on success
drjson_array_push_item(const DRJsonAllocator* allocator, DRJsonValue* array, DRJsonValue item){
    if(array->kind != DRJSON_ARRAY) return 1;
    if(array->capacity < array->count+1){
        if(array->capacity && !array->allocated){ // We don't own this buffer
            return 1;
        }
        size_t old_cap = array->capacity;
        enum {ARRAY_MAX = 0x1fffffff};
        size_t new_cap = old_cap?old_cap*2:4;
        if(new_cap > ARRAY_MAX) return 1;
        DRJsonValue* new_items = array->array_items?
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
drjson_object_set_item(const DRJsonAllocator* allocator, DRJsonValue* object, const char* key, size_t keylen, uint32_t hash, DRJsonValue item, _Bool copy){
    if(object->kind != DRJSON_OBJECT) return 1;
    enum {KEY_MAX = 0x7fffffff};
    enum {OBJECT_MAX = 0x1fffffff};
    if(keylen > KEY_MAX) return 1;
    if(!hash) hash = drjson_object_key_hash(key, keylen);
    if(unlikely(object->count *2 >= object->capacity)){
        if(!object->capacity){
            size_t new_cap = 4;
            DRJsonObjectPair* p = allocator->alloc(allocator->user_pointer, new_cap*sizeof(*p));
            if(!p) return 1;
            memset(p, 0, new_cap*sizeof(*p));
            object->object_items = p;
            object->allocated = 1;
            object->capacity = new_cap;
        }
        else {
            if(unlikely(!object->allocated)) return 1;
            size_t old_cap = object->capacity;
            size_t new_cap = old_cap * 2;
            if(new_cap > OBJECT_MAX) return 1;
            DRJsonObjectPair* p = allocator->alloc(allocator->user_pointer, new_cap*sizeof(*p));
            memset(p, 0, new_cap*sizeof(*p));
            for(size_t i = 0; i < old_cap; i++){
                DRJsonObjectPair o = object->object_items[i];
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
        DRJsonObjectPair* o = &object->object_items[idx];
        if(!o->key){
            if(copy){
                char* newkey = allocator->alloc(allocator->user_pointer, keylen);
                if(!newkey) return 1;
                memcpy(newkey, key, keylen);
                key = newkey;
            }
            *o = (DRJsonObjectPair){.key=key, .key_length=keylen, .key_hash=hash, .value=item, .key_allocated=copy};
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
drjson_object_set_item_no_copy_key(const DRJsonAllocator* allocator, DRJsonValue* object, const char* key, size_t keylen, uint32_t hash, DRJsonValue item){
    return drjson_object_set_item(allocator, object, key, keylen, hash, item, 0);
}
DRJSON_API 
int // 0 on success
drjson_object_set_item_copy_key(const DRJsonAllocator* allocator, DRJsonValue* object, const char* key, size_t keylen, uint32_t hash, DRJsonValue item){
    return drjson_object_set_item(allocator, object, key, keylen, hash, item, 1);
}
DRJSON_API 
DRJsonValue*_Nullable
drjson_object_get_item(DRJsonValue object, const char* key, size_t keylen, uint32_t hash){
    if(!hash) hash = drjson_object_key_hash(key, keylen);
    if(object.kind != DRJSON_OBJECT) return NULL;
    if(!object.capacity)
        return NULL;
    size_t idx = hash % object.capacity;
    for(;;){
        DRJsonObjectPair* o = &object.object_items[idx];
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
DRJsonValue*_Nullable
drjson_query(DRJsonValue v, const char* query, size_t length){
    enum {
        GETITEM,
        SUBSCRIPT,
        QUOTED_GETITEM,
    };
    int mode = GETITEM;
    size_t begin = 0;
    size_t i = 0;
    DRJsonValue* o = &v;
    for(;i != length; i++){
        char c = query[i];
        switch(c){
            case '[':
                if(mode == QUOTED_GETITEM) continue;
                if(i != begin){
                    if(mode != GETITEM) return NULL;
                    o = drjson_object_get_item(*o, query+begin, i-begin, 0);
                    if(!o) {
                        return NULL;
                    }
                }
                mode = SUBSCRIPT;
                begin = i +1;
                break;
            case '"':{
                if(i == begin){
                    mode = QUOTED_GETITEM;
                    begin = i+1;
                    break;
                }
                if(mode != QUOTED_GETITEM){
                    fprintf(stderr, "%d\n", __LINE__);
                    return NULL;
                }
                assert(i != begin);
                size_t nbackslash = 0;
                for(size_t back = i-1; back != begin; i--){
                    if(query[back] != '\\') break;
                    nbackslash++;
                }
                if(nbackslash & 1) continue;
                o = drjson_object_get_item(*o, query+begin, i-begin, 0);
                if(!o) {
                    fprintf(stderr, "%d\n", __LINE__);
                    return NULL;
                }
                mode = GETITEM;
                begin = i+1;
            }break;
            case ']':
                if(mode == QUOTED_GETITEM) continue;
                if(mode != SUBSCRIPT){
                    fprintf(stderr, "%d\n", __LINE__);
                    return NULL;
                }
                // lazy
                int index = atoi(query+begin);
                if(o->kind != DRJSON_ARRAY) {
                    fprintf(stderr, "%d\n", __LINE__);
                    return NULL;
                }
                if(index < 0 || index >= o->count) {
                    fprintf(stderr, "%d\n", __LINE__);
                    return NULL;
                }
                o = &o->array_items[index];
                begin = i+1;
                break;
            case '.':
                if(mode == QUOTED_GETITEM) continue;
                if(i != begin){
                    if(mode != GETITEM) {
                        fprintf(stderr, "%d\n", __LINE__);
                        return NULL;
                    }
                    o = drjson_object_get_item(*o, query+begin, i-begin, 0);
                    if(!o) {
                        fprintf(stderr, "%d\n", __LINE__);
                        return NULL;
                    }
                }
                mode = GETITEM;
                begin = i + 1;
                break;
            default:
                continue;
        }
    }
    if(i != begin){
        if(mode != GETITEM) {
            fprintf(stderr, "%d\n", __LINE__);
            return NULL;
        }
        o = drjson_object_get_item(*o, query+begin, i-begin, 0);
        if(!o) {
            fprintf(stderr, "%d\n", __LINE__);
            return NULL;
        }
    }
    if(o == &v) {
        fprintf(stderr, "%d\n", __LINE__);
        return NULL;
    }
    return o;
}



#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
