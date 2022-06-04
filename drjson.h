#ifndef DRJSON_H
#define DRJSON_H
#include <stddef.h> // size_t
#include <stdint.h>
#include <string.h>

#ifndef CJSON_API
#define CJSON_API extern
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

enum CJsonKind {
    CJSON_NUMBER,
    CJSON_INTEGER,
    CJSON_UINTEGER,
    CJSON_STRING,
    CJSON_ARRAY,
    CJSON_OBJECT,
    CJSON_NULL,
    CJSON_BOOL,
    CJSON_ERROR,
};

static const char*_Nonnull const CJsonKindNames[] = {
    [CJSON_NUMBER] = "number",
    [CJSON_INTEGER] = "integer", 
    [CJSON_UINTEGER] = "uinteger", 
    [CJSON_STRING] = "string",
    [CJSON_ARRAY] = "array",
    [CJSON_OBJECT] = "object",
    [CJSON_NULL] = "null", 
    [CJSON_BOOL] = "bool", 
    [CJSON_ERROR] = "error",
};

enum CJsonErrorCode {
    CJSON_ERROR_NONE = 0,
    CJSON_ERROR_UNEXPECTED_EOF = 1,
    CJSON_ERROR_ALLOC_FAILURE = 2,
    CJSON_ERROR_MISSING_KEY = 3,
    CJSON_ERROR_INDEX_ERROR = 4,
    CJSON_ERROR_INVALID_START_CHAR = 5,
};

typedef struct CJsonValue CJsonValue;
typedef struct CJsonObjectPair CJsonObjectPair;
struct CJsonValue {
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
        CJsonValue* array_items; // count is how many there are, capacity is length of the allocation
        CJsonObjectPair* object_items;
        int error_code;
        _Bool boolean;
        // Null is implicit
    };
};

typedef struct CJsonObjectPair CJsonObjectPair;
struct CJsonObjectPair {
    uint32_t key_length: 31;
    uint32_t key_allocated: 1;
    uint32_t key_hash;
    const char* key;
    CJsonValue value;
};


typedef struct CJsonAllocator CJsonAllocator;
struct CJsonAllocator {
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


typedef struct CJsonParseContext CJsonParseContext;
struct CJsonParseContext {
    const char* cursor;
    const char* end;
    const char* begin;
    int depth;
    const char* error_message; // static string
    const char* err_loc; // for reporting where the error occurred.
    CJsonAllocator allocator;
};

static inline
CJsonValue
cjson_make_error(enum CJsonErrorCode error){
    return (CJsonValue){.kind=CJSON_ERROR, .error_code=error};
}

static inline
CJsonValue
cjson_make_null(void){
    return (CJsonValue){.kind=CJSON_NULL};
}

static inline
CJsonValue
cjson_make_bool(_Bool b){
    return (CJsonValue){.kind=CJSON_BOOL, .boolean=b};
}

static inline
CJsonValue
cjson_make_number(double d){
    return (CJsonValue){.kind=CJSON_NUMBER, .number=d};
}

static inline
CJsonValue
cjson_make_int(int64_t i){
    return (CJsonValue){.kind=CJSON_INTEGER, .integer=i};
}

static inline
CJsonValue
cjson_make_uint(int64_t u){
    return (CJsonValue){.kind=CJSON_UINTEGER, .uinteger=u};
}

static inline
CJsonValue
cjson_make_object(const CJsonAllocator* allocator, size_t initial_length){
    CJsonObjectPair* items = allocator->alloc(allocator->user_pointer, initial_length*sizeof(*items));
    if(!items) return (CJsonValue){.kind=CJSON_ERROR, .error_code=CJSON_ERROR_ALLOC_FAILURE};
    return (CJsonValue){.kind=CJSON_OBJECT, .count=0, .capacity=initial_length, .object_items=items, .allocated=1};
}

#define cjson_make(x) _Generic(x, \
        int64_t: cjson_make_int, \
        uint64_t: cjson_make_uint, \
        double: cjson_make_number, \
        _Bool: cjson_make_bool)(x)

static inline
CJsonValue
cjson_make_array(const CJsonAllocator* allocator, size_t initial_length){
    CJsonValue* items = allocator->alloc(allocator->user_pointer, initial_length*sizeof(*items));
    if(!items) return (CJsonValue){.kind=CJSON_ERROR, .error_code=CJSON_ERROR_ALLOC_FAILURE};
    return (CJsonValue){.kind=CJSON_ARRAY, .count=0, .capacity=initial_length, .array_items=items, .allocated=1};
}

// NOTE:
// The string needs to be one that does not need to be escaped.
static inline
CJsonValue
cjson_make_string_no_copy(const char* s, size_t length){
    return (CJsonValue){.kind=CJSON_STRING, .count=length, .string=s};
}
static inline
CJsonValue
cjson_make_string_copy(const CJsonAllocator* allocator, const char* s, size_t length){
    char* string = allocator->alloc(allocator->user_pointer, length);
    if(!string) return (CJsonValue){.kind=CJSON_ERROR, .error_code=CJSON_ERROR_ALLOC_FAILURE};
    memcpy(string, s, length);
    return (CJsonValue){.kind=CJSON_STRING, .count=length, .allocated=1, .string=string};
}

CJSON_API
CJsonValue
cjson_parse(CJsonParseContext* ctx);

CJSON_API 
uint32_t
cjson_object_key_hash( const char* key, size_t keylen);

CJSON_API 
CJsonValue* _Nullable
cjson_object_get_item(CJsonValue object, const char* key, size_t keylen, uint32_t hash); // hash can be 0, which means "calculate for me"

CJSON_API 
int // 0 on success
cjson_object_set_item_copy_key(const CJsonAllocator* allocator, CJsonValue* object, const char* key, size_t keylen, uint32_t hash, CJsonValue item);
//
// hash can be 0 - means calculate it for me
//
CJSON_API 
int // 0 on success
cjson_object_set_item_no_copy_key(const CJsonAllocator* allocator, CJsonValue* object, const char* key, size_t keylen, uint32_t hash, CJsonValue item);

CJSON_API 
int // 0 on success
cjson_array_push_item(const CJsonAllocator* allocator, CJsonValue* array, CJsonValue item);

CJSON_API
CJsonAllocator
cjson_stdc_allocator(void);

// If your allocator doesn't have a fast free all, you can use this instead.
CJSON_API
void
cjson_slow_recursive_free_all(const CJsonAllocator* allocator, CJsonValue value);

CJSON_API
CJsonValue*_Nullable
cjson_query(CJsonValue v, const char* query, size_t length);

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
