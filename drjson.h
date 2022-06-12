#ifndef DRJSON_H
#define DRJSON_H
#include <stddef.h> // size_t
#include <stdint.h>
#include <string.h>

// Define DRJSON_NO_STDIO to avoid import stdio
#ifndef DRJSON_NO_STDIO
#include <stdio.h>
#endif

#ifndef DRJSON_API

#ifndef DRJSON_STATIC_LIB

#ifdef _WIN32
#define DRJSON_API extern __declspect(dllimport)
#else
#define DRJSON_API extern __attribute__((visibility("default")))
#endif

#else
#define DRJSON_API extern __attribute__((visibility("hidden")))
#endif

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

enum DrJsonKind {
    DRJSON_NUMBER,
    DRJSON_INTEGER,
    DRJSON_UINTEGER,
    DRJSON_STRING,
    DRJSON_ARRAY,
    DRJSON_OBJECT,
    DRJSON_NULL,
    DRJSON_BOOL,
    DRJSON_CAPSULE, // has a pointer to a C object.
    DRJSON_BOXED, // pointer to a drjson value
    DRJSON_ERROR,
};

static const char*_Nonnull const DrJsonKindNames[] = {
    [DRJSON_NUMBER] = "number",
    [DRJSON_INTEGER] = "integer", 
    [DRJSON_UINTEGER] = "uinteger", 
    [DRJSON_STRING] = "string",
    [DRJSON_ARRAY] = "array",
    [DRJSON_OBJECT] = "object",
    [DRJSON_NULL] = "null", 
    [DRJSON_BOOL] = "bool", 
    [DRJSON_CAPSULE] = "capsule",
    [DRJSON_BOXED] = "boxed",
    [DRJSON_ERROR] = "error",
};

enum DrJsonErrorCode {
    DRJSON_ERROR_NONE = 0,
    DRJSON_ERROR_UNEXPECTED_EOF = 1,
    DRJSON_ERROR_ALLOC_FAILURE = 2,
    DRJSON_ERROR_MISSING_KEY = 3,
    DRJSON_ERROR_INDEX_ERROR = 4,
    DRJSON_ERROR_INVALID_CHAR = 5,
    DRJSON_ERROR_INVALID_VALUE = 6,
    DRJSON_ERROR_INVALID_ERROR = 7,
};

static const char*_Nonnull const DrJsonErrorNames[] = {
    [DRJSON_ERROR_NONE] = "No error",
    [DRJSON_ERROR_UNEXPECTED_EOF] = "Unexpected End of Input",
    [DRJSON_ERROR_ALLOC_FAILURE] = "Allocation Failure",
    [DRJSON_ERROR_MISSING_KEY] = "Missing Key",
    [DRJSON_ERROR_INDEX_ERROR] = "Index Error",
    [DRJSON_ERROR_INVALID_CHAR] = "Invalid Char",
    [DRJSON_ERROR_INVALID_VALUE] = "Invalid Value",
    [DRJSON_ERROR_INVALID_ERROR] = "Error is Invalid",
};

typedef struct DrJsonValue DrJsonValue;
typedef struct DrJsonObjectPair DrJsonObjectPair;
struct DrJsonValue {
    uint64_t kind:4;
    uint64_t count: 29;
    uint64_t capacity: 29; // NOTE: this contains the error code
    uint64_t allocated: 1;
    // 1 bit unused
    union {
        double number;
        int64_t integer;
        uint64_t uinteger;
        const char* string; // not nul-terminated, use count.
        DrJsonValue* array_items; // count is how many there are, capacity is length of the allocation
        DrJsonObjectPair* object_items;
        const char* err_mess; // pointer to a c-string literal, nul terminated,
                              // but count is also its length
        _Bool boolean;
        void* capsule;
        DrJsonValue* boxed;
        // Null is implicit
    };
};

static inline
enum DrJsonErrorCode
drjson_get_error_code(DrJsonValue v){
    if(v.kind != DRJSON_ERROR)
        return DRJSON_ERROR_NONE;
    if(v.capacity >= DRJSON_ERROR_INVALID_ERROR)
        return DRJSON_ERROR_INVALID_ERROR;
    return v.capacity;
}
static inline
const char* 
drjson_get_error_name(DrJsonValue v){
    return DrJsonErrorNames[drjson_get_error_code(v)];
}

typedef struct DrJsonObjectPair DrJsonObjectPair;
struct DrJsonObjectPair {
    uint32_t key_length: 31;
    uint32_t key_allocated: 1;
    uint32_t key_hash;
    const char* key;
    DrJsonValue value;
};


typedef struct DrJsonAllocator DrJsonAllocator;
struct DrJsonAllocator {
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


typedef struct DrJsonParseContext DrJsonParseContext;
struct DrJsonParseContext {
    const char* cursor;
    const char* end;
    const char* begin;
    int depth;
    const char* error_message; // static string
    const char* err_loc; // for reporting where the error occurred.
    DrJsonAllocator allocator;
};

static inline
DrJsonValue
drjson_make_error(enum DrJsonErrorCode error, const char* mess){
    return (DrJsonValue){.kind=DRJSON_ERROR, .capacity=error, .err_mess=mess, .count=strlen(mess)};
}

static inline
DrJsonValue
drjson_make_null(void){
    return (DrJsonValue){.kind=DRJSON_NULL};
}

static inline
DrJsonValue
drjson_make_bool(_Bool b){
    return (DrJsonValue){.kind=DRJSON_BOOL, .boolean=b};
}

static inline
DrJsonValue
drjson_make_number(double d){
    return (DrJsonValue){.kind=DRJSON_NUMBER, .number=d};
}

static inline
DrJsonValue
drjson_make_int(int64_t i){
    return (DrJsonValue){.kind=DRJSON_INTEGER, .integer=i};
}

static inline
DrJsonValue
drjson_make_uint(int64_t u){
    return (DrJsonValue){.kind=DRJSON_UINTEGER, .uinteger=u};
}

static inline
DrJsonValue
drjson_make_capsule(void* p){
    return (DrJsonValue){.kind=DRJSON_CAPSULE, .capsule=p};
}

static inline
DrJsonValue
drjson_make_box(DrJsonValue* p){
    return (DrJsonValue){.kind=DRJSON_BOXED, .capsule=p};
}

static inline
DrJsonValue
drjson_make_object(const DrJsonAllocator* allocator, size_t initial_length){
    DrJsonObjectPair* items = allocator->alloc(allocator->user_pointer, initial_length*sizeof(*items));
    if(!items) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, 
        "Failed to allocate memory for object");
    return (DrJsonValue){.kind=DRJSON_OBJECT, .count=0, .capacity=initial_length, .object_items=items, .allocated=1};
}

#define drjson_make(x) _Generic(x, \
        int64_t: drjson_make_int, \
        uint64_t: drjson_make_uint, \
        double: drjson_make_number, \
        _Bool: drjson_make_bool)(x)

static inline
DrJsonValue
drjson_make_array(const DrJsonAllocator* allocator, size_t initial_length){
    DrJsonValue* items = allocator->alloc(allocator->user_pointer, initial_length*sizeof(*items));
    if(!items) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate memory for array");
    return (DrJsonValue){.kind=DRJSON_ARRAY, .count=0, .capacity=initial_length, .array_items=items, .allocated=1};
}

// NOTE:
// The string needs to be one that does not need to be escaped.
static inline
DrJsonValue
drjson_make_string_no_copy(const char* s, size_t length){
    return (DrJsonValue){.kind=DRJSON_STRING, .count=length, .string=s};
}
static inline
DrJsonValue
drjson_make_string_copy(const DrJsonAllocator* allocator, const char* s, size_t length){
    char* string = allocator->alloc(allocator->user_pointer, length);
    if(!string) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate storage for string");
    memcpy(string, s, length);
    return (DrJsonValue){.kind=DRJSON_STRING, .count=length, .allocated=1, .string=string};
}

DRJSON_API
DrJsonValue
drjson_parse(DrJsonParseContext* ctx);

DRJSON_API
DrJsonValue
drjson_parse_braceless_object(DrJsonParseContext* ctx);

DRJSON_API 
uint32_t
drjson_object_key_hash( const char* key, size_t keylen);

DRJSON_API 
DrJsonValue* _Nullable
drjson_object_get_item(DrJsonValue object, const char* key, size_t keylen, uint32_t hash); // hash can be 0, which means "calculate for me"

DRJSON_API 
int // 0 on success
drjson_object_set_item_copy_key(const DrJsonAllocator* allocator, DrJsonValue* object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item);
//
// hash can be 0 - means calculate it for me
//
DRJSON_API 
int // 0 on success
drjson_object_set_item_no_copy_key(const DrJsonAllocator* allocator, DrJsonValue* object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item);

DRJSON_API 
int // 0 on success
drjson_array_push_item(const DrJsonAllocator* allocator, DrJsonValue* array, DrJsonValue item);

DRJSON_API
DrJsonAllocator
drjson_stdc_allocator(void);

// If your allocator doesn't have a fast free all, you can use this instead.
DRJSON_API
void
drjson_slow_recursive_free_all(const DrJsonAllocator* allocator, DrJsonValue value);

DRJSON_API
DrJsonValue
drjson_query(DrJsonValue* v, const char* query, size_t length);

DRJSON_API
DrJsonValue
drjson_checked_query(DrJsonValue* v, int type, const char* query, size_t length);

DRJSON_API
DrJsonValue // returns an array or an error
drjson_multi_query(const DrJsonAllocator*_Nullable allocator, DrJsonValue* v, const char* query, size_t length);

#ifndef DRJSON_NO_STDIO
// returns negative value if an error was printed. (like fprintf, etc.)
enum {
    DRJSON_PRETTY_PRINT = 0x1,
};
DRJSON_API
int
drjson_print_value(FILE* fp, DrJsonValue v, int indent, unsigned flags );
#endif

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
