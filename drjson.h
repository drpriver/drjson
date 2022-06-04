#ifndef DRJSON_H
#define DRJSON_H
#include <stddef.h> // size_t
#include <stdint.h>
#include <string.h>

#ifndef DRJSON_API
#define DRJSON_API extern
#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nonnull
#define _Nonnull
#endif
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Null_unspecified
#define _Null_unspecified
#endif
#endif

enum DRJsonKind {
    DRJSON_NUMBER,
    DRJSON_INTEGER,
    DRJSON_UINTEGER,
    DRJSON_STRING,
    DRJSON_ARRAY,
    DRJSON_OBJECT,
    DRJSON_NULL,
    DRJSON_BOOL,
    DRJSON_ERROR,
};

static const char*_Nonnull const DRJsonKindNames[] = {
    [DRJSON_NUMBER] = "number",
    [DRJSON_INTEGER] = "integer", 
    [DRJSON_UINTEGER] = "uinteger", 
    [DRJSON_STRING] = "string",
    [DRJSON_ARRAY] = "array",
    [DRJSON_OBJECT] = "object",
    [DRJSON_NULL] = "null", 
    [DRJSON_BOOL] = "bool", 
    [DRJSON_ERROR] = "error",
};

enum DRJsonErrorCode {
    DRJSON_ERROR_NONE = 0,
    DRJSON_ERROR_UNEXPECTED_EOF = 1,
    DRJSON_ERROR_ALLOC_FAILURE = 2,
    DRJSON_ERROR_MISSING_KEY = 3,
    DRJSON_ERROR_INDEX_ERROR = 4,
    DRJSON_ERROR_INVALID_START_CHAR = 5,
};

typedef struct DRJsonValue DRJsonValue;
typedef struct DRJsonObjectPair DRJsonObjectPair;
struct DRJsonValue {
    uint64_t kind:4;
    uint64_t count: 29;
    uint64_t capacity: 29;
    uint64_t allocated: 1;
    // 1 bit unused
    union {
        double number;
        int64_t integer;
        uint64_t uinteger;
        const char* string; // not nul-terminated, use count.
        DRJsonValue* array_items; // count is how many there are, capacity is length of the allocation
        DRJsonObjectPair* object_items;
        int error_code;
        _Bool boolean;
        // Null is implicit
    };
};

typedef struct DRJsonObjectPair DRJsonObjectPair;
struct DRJsonObjectPair {
    uint32_t key_length: 31;
    uint32_t key_allocated: 1;
    uint32_t key_hash;
    const char* key;
    DRJsonValue value;
};


typedef struct DRJsonAllocator DRJsonAllocator;
struct DRJsonAllocator {
    // NOTE: if these functions are NULL, malloc, realloc and free from libc will be used.
    void*_Nullable user_pointer;
    void*_Nullable (*_Nonnull alloc)(void*_Null_unspecified user_pointer, size_t size);
    void*_Nullable (*_Nonnull realloc)(void*_Null_unspecified user_pointer, void*_Nullable old_pointer, size_t old_size, size_t new_size);
    void  (*_Nonnull free)(void*_Null_unspecified user_pointer, const void*_Nullable to_free, size_t size);
    // This function can be missing. If it is implemented and an error occurs
    // during parsing, it will be called to quickly free what has been
    // allocated.
    void (*_Nullable free_all)(void*_Null_unspecified user_pointer); 
};


typedef struct DRJsonParseContext DRJsonParseContext;
struct DRJsonParseContext {
    const char* cursor;
    const char* end;
    const char* begin;
    int depth;
    const char* error_message; // static string
    const char* err_loc; // for reporting where the error occurred.
    DRJsonAllocator allocator;
};

static inline
DRJsonValue
drjson_make_error(enum DRJsonErrorCode error){
    return (DRJsonValue){.kind=DRJSON_ERROR, .error_code=error};
}

static inline
DRJsonValue
drjson_make_null(void){
    return (DRJsonValue){.kind=DRJSON_NULL};
}

static inline
DRJsonValue
drjson_make_bool(_Bool b){
    return (DRJsonValue){.kind=DRJSON_BOOL, .boolean=b};
}

static inline
DRJsonValue
drjson_make_number(double d){
    return (DRJsonValue){.kind=DRJSON_NUMBER, .number=d};
}

static inline
DRJsonValue
drjson_make_int(int64_t i){
    return (DRJsonValue){.kind=DRJSON_INTEGER, .integer=i};
}

static inline
DRJsonValue
drjson_make_uint(int64_t u){
    return (DRJsonValue){.kind=DRJSON_UINTEGER, .uinteger=u};
}

static inline
DRJsonValue
drjson_make_object(const DRJsonAllocator* allocator, size_t initial_length){
    DRJsonObjectPair* items = allocator->alloc(allocator->user_pointer, initial_length*sizeof(*items));
    if(!items) return (DRJsonValue){.kind=DRJSON_ERROR, .error_code=DRJSON_ERROR_ALLOC_FAILURE};
    return (DRJsonValue){.kind=DRJSON_OBJECT, .count=0, .capacity=initial_length, .object_items=items, .allocated=1};
}

#define drjson_make(x) _Generic(x, \
        int64_t: drjson_make_int, \
        uint64_t: drjson_make_uint, \
        double: drjson_make_number, \
        _Bool: drjson_make_bool)(x)

static inline
DRJsonValue
drjson_make_array(const DRJsonAllocator* allocator, size_t initial_length){
    DRJsonValue* items = allocator->alloc(allocator->user_pointer, initial_length*sizeof(*items));
    if(!items) return (DRJsonValue){.kind=DRJSON_ERROR, .error_code=DRJSON_ERROR_ALLOC_FAILURE};
    return (DRJsonValue){.kind=DRJSON_ARRAY, .count=0, .capacity=initial_length, .array_items=items, .allocated=1};
}

// NOTE:
// The string needs to be one that does not need to be escaped.
static inline
DRJsonValue
drjson_make_string_no_copy(const char* s, size_t length){
    return (DRJsonValue){.kind=DRJSON_STRING, .count=length, .string=s};
}
static inline
DRJsonValue
drjson_make_string_copy(const DRJsonAllocator* allocator, const char* s, size_t length){
    char* string = allocator->alloc(allocator->user_pointer, length);
    if(!string) return (DRJsonValue){.kind=DRJSON_ERROR, .error_code=DRJSON_ERROR_ALLOC_FAILURE};
    memcpy(string, s, length);
    return (DRJsonValue){.kind=DRJSON_STRING, .count=length, .allocated=1, .string=string};
}

DRJSON_API
DRJsonValue
drjson_parse(DRJsonParseContext* ctx);

DRJSON_API 
uint32_t
drjson_object_key_hash( const char* key, size_t keylen);

DRJSON_API 
DRJsonValue* _Nullable
drjson_object_get_item(DRJsonValue object, const char* key, size_t keylen, uint32_t hash); // hash can be 0, which means "calculate for me"

DRJSON_API 
int // 0 on success
drjson_object_set_item_copy_key(const DRJsonAllocator* allocator, DRJsonValue* object, const char* key, size_t keylen, uint32_t hash, DRJsonValue item);
//
// hash can be 0 - means calculate it for me
//
DRJSON_API 
int // 0 on success
drjson_object_set_item_no_copy_key(const DRJsonAllocator* allocator, DRJsonValue* object, const char* key, size_t keylen, uint32_t hash, DRJsonValue item);

DRJSON_API 
int // 0 on success
drjson_array_push_item(const DRJsonAllocator* allocator, DRJsonValue* array, DRJsonValue item);

DRJSON_API
DRJsonAllocator
drjson_stdc_allocator(void);

// If your allocator doesn't have a fast free all, you can use this instead.
DRJSON_API
void
drjson_slow_recursive_free_all(const DRJsonAllocator* allocator, DRJsonValue value);

DRJSON_API
DRJsonValue*_Nullable
drjson_query(DRJsonValue v, const char* query, size_t length);

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
