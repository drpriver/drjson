//
// Copyright © 2022, David Priver
//
#ifndef DRJSON_H
#define DRJSON_H
#include <stddef.h> // size_t
#include <stdint.h>
#include <string.h>

#define DRJSON_VERSION "2.0.0"

#ifndef drj_memcpy
#if !defined(__GNUC__) || defined(__IMPORTC__)
#define drj_memset memset
#define drj_memcpy memcpy
#define drj_memmove memmove
#else
#define drj_memset __builtin_memset
#define drj_memcpy __builtin_memcpy
#define drj_memmove __builtin_memmove
#endif
#endif

// Define DRJSON_NO_STDIO to avoid import stdio
#ifndef DRJSON_NO_STDIO
#include <stdio.h>
#endif

#ifndef DRJSON_API

#ifndef DRJSON_STATIC_LIB

#ifdef _WIN32
#define DRJSON_API extern __declspec(dllimport)
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

// can fit in 4 bits
enum DrJsonKind {
    DRJSON_ERROR         = 0x0, // error code + message
    DRJSON_NUMBER        = 0x1, // double
    DRJSON_INTEGER       = 0x2, // int64_t
    DRJSON_UINTEGER      = 0x3, // uint64_t
    DRJSON_STRING        = 0x4, // const char* + length
    DRJSON_ARRAY         = 0x5, // index
    DRJSON_OBJECT        = 0x6, // index
    DRJSON_NULL          = 0x7,
    DRJSON_BOOL          = 0x8, //
    DRJSON_ARRAY_VIEW    = 0x9,
    DRJSON_OBJECT_KEYS   = 0xa,
    DRJSON_OBJECT_VALUES = 0xb,
    DRJSON_OBJECT_ITEMS  = 0xc,
};


typedef struct DrJsonValue DrJsonValue;
struct DrJsonValue {
    union {
        uint64_t bits;
#ifndef DRJSON_OPAQUE_BITFIELDS
        struct {
            uint64_t kind :4;
            uint64_t :60;
        };
        struct {
            uint64_t _skind:4;
            uint64_t slen :60;
        };
        struct {
            uint64_t _ekind:4;
            uint64_t error_code :4;
            uint64_t err_len :56;
        };
#endif
    };
    union {
        // DRJSON_NUMBER
        double number;

        // DRJSON_INTEGER
        int64_t integer;

        // DRJSON_UINTEGER
        uint64_t uinteger;

        // DRJSON_STRING
        const char* string; // not nul-terminated, use slen.
                            //
        // DRJSON_ERROR
        const char* err_mess; // pointer to a c-string literal, nul terminated. Length is also given.

        // DRJSON_BOOL
        _Bool boolean;

        // DRJSON_ARRAY
        // DRJSON_ARRAY_VIEW
        size_t array_idx;

        // DRJSON_OBJECT
        // DRJSON_OBJECT_KEYS
        // DRJSON_OBJECT_VALUES
        // DRJSON_OBJECT_ITEMS
        size_t object_idx;

        // null is implicit
    };
};

_Static_assert(sizeof(DrJsonValue) == 2*sizeof(void*), "");


DRJSON_API
int
drjson_kind(DrJsonValue v);
// Retrieves the `kind` field from a json value.
//
// If DRJSON_OPAQUE_BITFIELDS is not defined, this is replaced with a macro.
// This is provided for interfacing with compilers/languages that can't handle
// bitfields.

DRJSON_API
int
drjson_error_code(DrJsonValue v);
// Retrieves the `error_code` field from a json value.
// It is only valid to call this if `kind` is DRJSON_ERROR.
//
// If DRJSON_OPAQUE_BITFIELDS is not defined, this is replaced with a macro.
// This is provided for interfacing with compilers/languages that can't handle
// bitfields.

DRJSON_API
size_t
drjson_slen(DrJsonValue v);
// Retrieves the `slen` field from a json value.
// It is only valid to call this if `kind` is DRJSON_STRING.
//
// If DRJSON_OPAQUE_BITFIELDS is not defined, this is replaced with a macro.
// This is provided for interfacing with compilers/languages that can't handle
// bitfields.

DRJSON_API
const char*
drjson_error_mess(DrJsonValue v);
// Retrieves the `err_mess` field from a json value.
// It is only valid to call this if `kind` is DRJSON_STRING.
//
// If DRJSON_OPAQUE_BITFIELDS is not defined, this is replaced with a macro.
// This is provided for interfacing with compilers/languages that can't handle
// bitfields.

#ifndef DRJSON_OPAQUE_BITFIELDS
#define drjson_kind(v) (v).kind
#define drjson_error_code(v) (v).error_code
#define drjson_error_mess(v) (v).err_mess
#define drjson_slen(v) (v).slen
#endif

typedef struct DrJsonAllocator DrJsonAllocator;
// To use a custom allocator, fill out this struct.
// If you just want to use malloc, etc. just use `drjson_stdc_allocator`.
struct DrJsonAllocator {
    void*_Nullable user_pointer;
    void*_Nullable (*_Nonnull alloc)(void*_Null_unspecified user_pointer, size_t size);
    void*_Nullable (*_Nonnull realloc)(void*_Null_unspecified user_pointer, void*_Nullable old_pointer, size_t old_size, size_t new_size);
    void  (*_Nonnull free)(void*_Null_unspecified user_pointer, const void*_Nullable to_free, size_t size);
    // This function can be missing. If it is implemented and an error occurs
    // during parsing, it will be called to quickly free what has been
    // allocated.
    void (*_Nullable free_all)(void*_Null_unspecified user_pointer);
};


DRJSON_API
DrJsonAllocator
drjson_stdc_allocator(void);
// A default allocator that just calls malloc, free, etc.

typedef struct DrJsonStringNode DrJsonStringNode;
struct DrJsonStringNode {
    DrJsonStringNode*_Nullable next;
    size_t data_length; // sizeof node is sizeof(DrJsonStringNode) + data_length
    char data[];
};

typedef struct DrJsonContext DrJsonContext;
struct DrJsonContext {
    // initialize with your allocator
    DrJsonAllocator allocator;
    // initialize below to 0
    DrJsonStringNode* strings;
    struct {
        void* data;
        size_t count;
        size_t capacity;
    } objects;
    struct {
        void* data;
        size_t count;
        size_t capacity;
    } arrays;
};

typedef struct DrJsonParseContext DrJsonParseContext;
struct DrJsonParseContext {
    const char* cursor; // initialize to string
    const char* end; // initialize to string + length
    const char* begin; // initialize to string
    int depth; // initialize to 0
    DrJsonContext* ctx; //
};

// Writer abstraction for serializing json.
typedef struct DrJsonTextWriter DrJsonTextWriter;
struct DrJsonTextWriter {
    void*_Null_unspecified up; //user pointer
    int (*write)(void*_Null_unspecified user_data, const void*, size_t);
};

static const char*_Nonnull const DrJsonKindNames[] = {
    [DRJSON_ERROR]         = "error",
    [DRJSON_NUMBER]        = "number",
    [DRJSON_INTEGER]       = "integer",
    [DRJSON_UINTEGER]      = "uinteger",
    [DRJSON_STRING]        = "string",
    [DRJSON_ARRAY]         = "array",
    [DRJSON_OBJECT]        = "object",
    [DRJSON_NULL]          = "null",
    [DRJSON_BOOL]          = "bool",
    [DRJSON_ARRAY_VIEW]    = "array view",
    [DRJSON_OBJECT_KEYS]   = "object keys",
    [DRJSON_OBJECT_VALUES] = "object values",
    [DRJSON_OBJECT_ITEMS]  = "object items",
};

static const size_t DrJsonKindNameLengths[] = {
    [DRJSON_ERROR]         = sizeof("error")-1,
    [DRJSON_NUMBER]        = sizeof("number")-1,
    [DRJSON_INTEGER]       = sizeof("integer")-1,
    [DRJSON_UINTEGER]      = sizeof("uinteger")-1,
    [DRJSON_STRING]        = sizeof("string")-1,
    [DRJSON_ARRAY]         = sizeof("array")-1,
    [DRJSON_OBJECT]        = sizeof("object")-1,
    [DRJSON_NULL]          = sizeof("null")-1,
    [DRJSON_BOOL]          = sizeof("bool")-1,
    [DRJSON_ARRAY_VIEW]    = sizeof("array view")-1,
    [DRJSON_OBJECT_KEYS]   = sizeof("object keys")-1,
    [DRJSON_OBJECT_VALUES] = sizeof("object values")-1,
    [DRJSON_OBJECT_ITEMS]  = sizeof("object items")-1,
};

enum DrJsonErrorCode {
    DRJSON_ERROR_NONE           = 0,
    DRJSON_ERROR_UNEXPECTED_EOF = 1,
    DRJSON_ERROR_ALLOC_FAILURE  = 2,
    DRJSON_ERROR_MISSING_KEY    = 3,
    DRJSON_ERROR_INDEX_ERROR    = 4,
    DRJSON_ERROR_INVALID_CHAR   = 5,
    DRJSON_ERROR_INVALID_VALUE  = 6,
    DRJSON_ERROR_TOO_DEEP       = 7,
    DRJSON_ERROR_TYPE_ERROR     = 8,
    DRJSON_ERROR_INVALID_ERROR  = 9,
};

static const char*_Nonnull const DrJsonErrorNames[] = {
    [DRJSON_ERROR_NONE]           = "No error",
    [DRJSON_ERROR_UNEXPECTED_EOF] = "Unexpected End of Input",
    [DRJSON_ERROR_ALLOC_FAILURE]  = "Allocation Failure",
    [DRJSON_ERROR_MISSING_KEY]    = "Missing Key",
    [DRJSON_ERROR_INDEX_ERROR]    = "Index Error",
    [DRJSON_ERROR_INVALID_CHAR]   = "Invalid Char",
    [DRJSON_ERROR_INVALID_VALUE]  = "Invalid Value",
    [DRJSON_ERROR_TOO_DEEP]       = "Too Many Levels of Nesting",
    [DRJSON_ERROR_TYPE_ERROR]     = "Invalid type for operation",
    [DRJSON_ERROR_INVALID_ERROR]  = "Error is Invalid",
};

static const size_t DrJsonErrorNameLengths[] = {
    [DRJSON_ERROR_NONE]           = sizeof("No error")-1,
    [DRJSON_ERROR_UNEXPECTED_EOF] = sizeof("Unexpected End of Input")-1,
    [DRJSON_ERROR_ALLOC_FAILURE]  = sizeof("Allocation Failure")-1,
    [DRJSON_ERROR_MISSING_KEY]    = sizeof("Missing Key")-1,
    [DRJSON_ERROR_INDEX_ERROR]    = sizeof("Index Error")-1,
    [DRJSON_ERROR_INVALID_CHAR]   = sizeof("Invalid Char")-1,
    [DRJSON_ERROR_INVALID_VALUE]  = sizeof("Invalid Value")-1,
    [DRJSON_ERROR_TOO_DEEP]       = sizeof("Too Many Levels of Nesting")-1,
    [DRJSON_ERROR_TYPE_ERROR]     = sizeof("Invalid type for operation")-1,
    [DRJSON_ERROR_INVALID_ERROR]  = sizeof("Error is Invalid")-1,
};

static inline
const char*
drjson_get_error_name(DrJsonValue v){
    return DrJsonErrorNames[drjson_error_code(v)];
}

static inline
size_t
drjson_get_error_name_length(DrJsonValue v){
    return DrJsonErrorNameLengths[drjson_error_code(v)];
}

static inline
int
drjson_is_numeric(DrJsonValue v){
    return drjson_kind(v) == DRJSON_NUMBER || drjson_kind(v) == DRJSON_INTEGER || drjson_kind(v) == DRJSON_UINTEGER;
}

// shallow equality (although it does compare strings)
static inline
int
drjson_eq(DrJsonValue a, DrJsonValue b){
    if(drjson_kind(a) != drjson_kind(b)) return 0;
    switch(drjson_kind(a)){
        case DRJSON_NUMBER:
            return a.number == b.number;
        case DRJSON_INTEGER:
            return a.integer == b.integer;
        case DRJSON_UINTEGER:
            return a.uinteger == b.uinteger;
        case DRJSON_STRING:
            if(drjson_slen(a) != drjson_slen(b))
                return 0;
            return memcmp(a.string, b.string, drjson_slen(a)) == 0;
        case DRJSON_ARRAY:
            return a.array_idx == b.array_idx;
        case DRJSON_OBJECT:
            return a.object_idx == b.object_idx;
        case DRJSON_NULL:
            return 1;
        case DRJSON_BOOL:
            return a.boolean == b.boolean;
        case DRJSON_ERROR:
            return drjson_error_code(a) == drjson_error_code(b);
        case DRJSON_ARRAY_VIEW:
            return a.array_idx == b.array_idx;
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_VALUES:
        case DRJSON_OBJECT_ITEMS:
            return a.object_idx == b.object_idx;
        default:
            return 0;
    }
}


#ifndef DRJSON_OPAQUE_BITFIELDS
static inline
DrJsonValue
drjson_make_error(enum DrJsonErrorCode error, const char* mess){
    return (DrJsonValue){._ekind=DRJSON_ERROR, .error_code=error, .err_mess=mess, .err_len=strlen(mess)};
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
#endif

static inline
size_t
drjson_size_for_object_of_length(size_t len){
    return 5*sizeof(void*)*len;
}

DRJSON_API
void
drjson_ctx_free_all(DrJsonContext* ctx);

DRJSON_API
DrJsonValue
drjson_make_object(DrJsonContext* ctx, size_t initial_length);

DRJSON_API
DrJsonValue
drjson_make_array(DrJsonContext* ctx, size_t initial_cap);


// Copies the string and tracks it in the ctx.
static inline
DrJsonStringNode*_Nullable
drjson_store_string_copy(DrJsonContext* ctx, const char* s, size_t length){
    if(!s || !length) return NULL;
    DrJsonStringNode* node = ctx->allocator.alloc(ctx->allocator.user_pointer, length + sizeof *node);
    if(!node) return NULL;
    drj_memcpy(node->data, s, length);
    node->data_length = length;
    node->next = ctx->strings;
    ctx->strings = node;
    return node;
}

#ifndef DRJSON_OPAQUE_BITFIELDS
// NOTE:
// The string needs to be one that does not need to be escaped.
static inline
DrJsonValue
drjson_make_string_no_copy(const char* s, size_t length){
    return (DrJsonValue){._skind=DRJSON_STRING, .slen=length, .string=s};
}

static inline
DrJsonValue
drjson_make_string_copy(DrJsonContext* ctx, const char* s, size_t length){
    DrJsonStringNode* node = drjson_store_string_copy(ctx, s, length);
    if(!node) return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to allocate storage for string");
    return drjson_make_string_no_copy(node->data, length);
}
#endif

// XXX: should these be static inline as they are trivial?

// returns an object_keys (which is array-like) or error
DRJSON_API
DrJsonValue
drjson_object_keys(DrJsonValue object);

// returns an object_values (which is array-like) or error
DRJSON_API
DrJsonValue
drjson_object_values(DrJsonValue object);

// returns an object_items (which is array-like) or error.
// The "array" is a flat array of alternating key value.
DRJSON_API
DrJsonValue
drjson_object_items(DrJsonValue object);

DRJSON_API
DrJsonValue
drjson_parse(DrJsonParseContext* ctx);

enum {
    DRJSON_PARSE_STRING_NONE = 0x0,
    // Copy the provided string into the context (so the context manages all lifetimes).
    DRJSON_PARSE_STRING_COPY = 0x1,
};

DRJSON_API
DrJsonValue
drjson_parse_string(DrJsonContext* ctx, const char* text, size_t length, uint32_t flags);

DRJSON_API
DrJsonValue
drjson_parse_braceless_object(DrJsonParseContext* ctx);

DRJSON_API
DrJsonValue
drjson_parse_braceless_string(DrJsonContext* ctx, const char* text, size_t length, uint32_t flags);

DRJSON_API
uint32_t
drjson_object_key_hash( const char* key, size_t keylen);

DRJSON_API
DrJsonValue
drjson_object_get_item(const DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, uint32_t hash); // hash can be 0, which means "calculate for me"

DRJSON_API
int // 0 on success
drjson_object_set_item_copy_key(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item);
//
// hash can be 0 - means calculate it for me
//
DRJSON_API
int // 0 on success
drjson_object_set_item_no_copy_key(DrJsonContext* ctx, DrJsonValue object, const char* key, size_t keylen, uint32_t hash, DrJsonValue item);

// Works on both arrays and array-likes (array view, object keys view, object
// values view, etc.)
// A negative idx means to idx from the back (last item is at -1).
DRJSON_API
DrJsonValue
drjson_get_by_index(const DrJsonContext* ctx, DrJsonValue v, int64_t idx);

// -1 if invalid error
DRJSON_API
int64_t
drjson_len(const DrJsonContext* ctx, DrJsonValue v);

DRJSON_API
int // 0 on success
drjson_array_push_item(const DrJsonContext* ctx, DrJsonValue array, DrJsonValue item);

DRJSON_API
DrJsonValue // returns DRJSON_ERROR if array is empty
drjson_array_pop_item(const DrJsonContext* ctx, DrJsonValue array);

DRJSON_API
DrJsonValue // returns DRJSON_ERROR if array is empty
drjson_array_del_item(const DrJsonContext* ctx, DrJsonValue array, size_t idx);

DRJSON_API
int // 0 on success
drjson_array_insert_item(const DrJsonContext* ctx, DrJsonValue array, size_t idx, DrJsonValue item);

DRJSON_API
DrJsonValue
drjson_query(const DrJsonContext* ctx, DrJsonValue v, const char* query, size_t query_length);

DRJSON_API
DrJsonValue
drjson_checked_query(const DrJsonContext* ctx, DrJsonValue v, int type, const char* query, size_t query_length);

DRJSON_API
int
drjson_escape_string(const DrJsonAllocator* restrict allocator, const char* restrict unescaped, size_t length, char *_Nullable restrict *_Nonnull restrict outstring, size_t* restrict outlength);

#if 0
DRJSON_API
int
drjson_unescape_string(const DrJsonAllocator* restrict allocator, const char* restrict unescaped, size_t length, char*_Nullable restrict *_Nonnull restrict outstring, size_t* restrict outlength);
#endif

enum {
    DRJSON_PRETTY_PRINT   = 0x1,
    DRJSON_APPEND_NEWLINE = 0x2,
    DRJSON_APPEND_ZERO    = 0x4,
};

// Returns 0 on success, 1 on error.
DRJSON_API
int
drjson_print_value(const DrJsonContext* ctx, const DrJsonTextWriter* writer, DrJsonValue v, int indent, unsigned flags);

DRJSON_API
int
drjson_print_value_mem(const DrJsonContext* ctx, void* buff, size_t bufflen, DrJsonValue v, int indent, unsigned flags, size_t*_Nullable printed);

DRJSON_API
void
drjson_get_line_column(const DrJsonParseContext* ctx, size_t* line, size_t* column);

// Returns 0 on success, 1 on error.
DRJSON_API
int
drjson_print_error(const DrJsonTextWriter* restrict writer, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v);

DRJSON_API
int
drjson_print_error_mem(void* buff, size_t bufflen, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v);

#ifndef DRJSON_NO_STDIO
// Like above, but for FILE*
DRJSON_API
int
drjson_print_value_fp(const DrJsonContext* ctx, FILE* fp, DrJsonValue v, int indent, unsigned flags);

DRJSON_API
int
drjson_print_error_fp(FILE* fp, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v);
#endif

#ifndef _WIN32
DRJSON_API
int
drjson_print_value_fd(const DrJsonContext* ctx, int fd, DrJsonValue v, int indent, unsigned flags);

DRJSON_API
int
drjson_print_error_fd(int fd, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v);
#else
DRJSON_API
int
drjson_print_value_HANDLE(const DrJsonContext* ctx, void* hnd, DrJsonValue v, int indent, unsigned flags);
DRJSON_API
int
drjson_print_error_HANDLE(void* hnd, const char* filename, size_t filename_len, size_t line, size_t column, DrJsonValue v);
#endif

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
