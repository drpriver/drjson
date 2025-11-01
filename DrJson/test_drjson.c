#ifdef _WIN32
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif
#ifdef DRJSON_UNITY
#define DRJSON_API static
#endif
#include "drjson.h"
#include "testing.h"
#include "../debugging.h"
#include "hash_func.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif
static TestFunc TestSimpleParsing;
static TestFunc TestSimpleParsing2;
static TestFunc TestIntern;
static TestFunc TestDoubleParsing;
static TestFunc TestSerialization;
static TestFunc TestPrettyPrint;
static TestFunc TestEscape;
static TestFunc TestObject;
static TestFunc TestPathParse;

int main(int argc, char*_Nullable*_Nonnull argv){
    RegisterTest(TestSimpleParsing);
    RegisterTest(TestSimpleParsing2);
    RegisterTest(TestIntern);
    RegisterTest(TestDoubleParsing);
    RegisterTest(TestSerialization);
    RegisterTest(TestPrettyPrint);
    RegisterTest(TestEscape);
    RegisterTest(TestObject);
    RegisterTest(TestPathParse);
    return test_main(argc, argv, NULL);
}

typedef struct Allocation Allocation;
struct Allocation {
    const void* ptr;
    size_t sz;
    _Bool freed;
    BacktraceArray* alloc_trace;
    BacktraceArray*_Null_unspecified free_trace;
};
static void
dump_a(const Allocation* a){
    fprintf(stderr, "Alloced at\n");
    dump_bt(a->alloc_trace);
    fprintf(stderr, "\n");
    if(a->free_trace){
        fprintf(stderr, "Freed at\n");
        dump_bt(a->free_trace);
        fprintf(stderr, "\n");
    }
}

enum {TEST_ALLOCATOR_CAP = 256*256*2};
typedef struct TestAllocator TestAllocator;
struct TestAllocator {
    size_t cursor;
    uint32_t idxes[TEST_ALLOCATOR_CAP*2];
    Allocation allocations[TEST_ALLOCATOR_CAP];
};
TestAllocator test_allocator;

static inline
uint32_t
hash_ptr(const void* ptr){
    uintptr_t data[1] = {(uintptr_t)ptr};
    uint32_t hash = hash_align8(data, sizeof data);
    return hash;
}

Allocation*
test_getsert(TestAllocator* ta, const void* ptr){
    uint32_t hash = hash_ptr(ptr);
    uint32_t idx = fast_reduce32(hash, TEST_ALLOCATOR_CAP*2);
    for(;;){
        uint32_t i = ta->idxes[idx];
        if(i == 0){ // unset
            assert(ta->cursor < TEST_ALLOCATOR_CAP);
            Allocation* a = &ta->allocations[ta->cursor++];
            ta->idxes[idx] = (uint32_t)ta->cursor; // index of slot +1
            *a = (Allocation){
                .ptr = ptr,
            };
            return a;
        }
        Allocation* a = &ta->allocations[i-1];
        if(a->ptr == ptr){
            return a;
        }
        idx++;
        if(idx >= TEST_ALLOCATOR_CAP*2) idx = 0;
    }
}

Allocation*_Nullable
test_get(TestAllocator* ta, const void* ptr){
    uint32_t hash = hash_ptr(ptr);
    uint32_t idx = fast_reduce32(hash, TEST_ALLOCATOR_CAP*2);
    for(;;){
        uint32_t i = ta->idxes[idx];
        if(i == 0){ // unset
            return NULL;
        }
        Allocation* a = &ta->allocations[i-1];
        if(a->ptr == ptr){
            return a;
        }
        idx++;
        if(idx >= TEST_ALLOCATOR_CAP*2) idx = 0;
    }
}

static
void* _Nullable
test_alloc(void* up, size_t size){
    TestAllocator* ta = up;
    void* p = malloc(size);
    if(!p) return NULL;
    assert(ta->cursor < 256*256*2);
    Allocation* a = test_getsert(ta, p);
    a->sz = size;
    if(a->free_trace) free_bt(a->free_trace);
    a->free_trace = NULL;
    a->freed = 0;
    a->alloc_trace = get_bt();
    return p;
}

static
void
record_free(void* up, const void*_Null_unspecified ptr, size_t size){
    if(!ptr) return;
    TestAllocator* ta = up;
    Allocation* a = test_get(ta, ptr);
    if(!a){
        bt();
        assert(!"freeing wild pointer");
    }
    if(a->sz != size){
        dump_a(a);
        fprintf(stderr, "Freed at\n");
        bt();
        assert(!"Freeing with wrong size");
    }
    if(a->freed){
        dump_a(a);
        fprintf(stderr, "Freed again at\n");
        bt();
        fprintf(stderr, "\n");
        assert(!"Double free");
    }
    a->freed = 1;
    a->free_trace = get_bt();
}

static
void
test_free(void* up, const void*_Null_unspecified ptr, size_t size){
    record_free(up, ptr, size);
    free((void*)ptr);
}

static
void*_Nullable
test_realloc(void* up, void*_Nullable ptr, size_t old_sz, size_t new_sz){
    TestAllocator* ta = up;
    if(!ptr){
        assert(!old_sz);
        return test_alloc(up, new_sz);
    }
    assert(old_sz);
    if(!new_sz){
        test_free(up, ptr, old_sz);
        return NULL;
    }
    record_free(ta, ptr, old_sz);
    void* p = test_alloc(ta, new_sz);
    if(!p) return NULL;
    if(old_sz < new_sz)
        drj_memcpy(p, ptr, old_sz);
    else
        drj_memcpy(p, ptr, new_sz);
    free(ptr);

    Allocation* a = test_getsert(ta, p);
    a->sz = new_sz;
    assert(!a->freed);
    return p;
}


static
void
assert_all_freed(void){
    TestAllocator* ta = &test_allocator;
    for(size_t i = 0; i < ta->cursor; i++){
        Allocation * a = &ta->allocations[i];
        if(!a->freed){
            dump_a(a);
        }
        assert(ta->allocations[i].freed);
    }
}

static
DrJsonAllocator
get_test_allocator(void){
    assert_all_freed();
    TestAllocator* ta = &test_allocator;
    for(size_t i = 0; i < ta->cursor; i++){
        Allocation * a = &ta->allocations[i];
        if(a->alloc_trace) free_bt(a->alloc_trace);
        if(a->free_trace) free_bt(a->free_trace);
    }
    memset(&test_allocator, 0, sizeof test_allocator);
    return (DrJsonAllocator){
        .user_pointer = &test_allocator,
        .alloc = test_alloc,
        .realloc = test_realloc,
        .free = test_free,
    };
}
TestFunction(TestSimpleParsing){
    TESTBEGIN();
    const char* example = "{ hello world }";
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    DrJsonParseContext pctx = {
        .begin = example,
        .cursor = example,
        .end = example + strlen(example),
        .ctx = ctx,
    };
    DrJsonValue v = drjson_parse(&pctx, DRJSON_PARSE_FLAG_NO_COPY_STRINGS);
    TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
    TestAssertEquals((int)v.kind, DRJSON_OBJECT);

    DrJsonValue q = drjson_query(ctx, v, "hello", strlen("hello"));
    TestAssertNotEqual((int)q.kind, DRJSON_ERROR);
    TestAssertEquals((int)q.kind, DRJSON_STRING);
    const char* string = ""; size_t len = 0;
    int err = drjson_get_str_and_len(ctx, q, &string, &len);
    TestAssertFalse(err);
    TestAssertEquals(len, sizeof("world")-1);
    TestAssert(memcmp(string, "world", sizeof("world")-1)==0);

    DrJsonValue val = drjson_object_get_item(ctx, v, "hello", strlen("hello"));
    TestAssertNotEqual((int)val.kind, DRJSON_ERROR);
    TestAssert(drjson_eq(q, val));

    DrJsonAtom a;
    err = DRJSON_ATOMIZE(ctx, "hello", &a);
    TestAssertFalse(err);
    DrJsonValue val2 = drjson_object_get_item_atom(ctx, v, a);
    TestAssertNotEqual((int)val2.kind, DRJSON_ERROR);
    TestAssert(drjson_eq(q, val2));


    drjson_gc(ctx, (DrJsonValue[]){q, val2}, 2);
    TestAssert(drjson_eq(q, val2));
    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestSimpleParsing2){
    TESTBEGIN();
    const char* example = "[hello world]";
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    DrJsonParseContext pctx = {
        .begin = example,
        .cursor = example,
        .end = example + strlen(example),
        .ctx = ctx,
    };
    DrJsonValue v = drjson_parse(&pctx, DRJSON_PARSE_FLAG_NO_COPY_STRINGS);
    TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
    TestAssertEquals((int)v.kind, DRJSON_ARRAY);

    DrJsonValue q = drjson_query(ctx, v, "[1]", strlen("[1]"));
    TestAssertNotEqual((int)q.kind, DRJSON_ERROR);
    TestAssertEquals((int)q.kind, DRJSON_STRING);
    const char* string = ""; size_t len = 0;
    int err = drjson_get_str_and_len(ctx, q, &string, &len);
    TestAssertFalse(err);
    TestAssertEquals(len, sizeof("world")-1);
    TestAssert(memcmp(string, "world", sizeof("world")-1)==0);

    DrJsonValue val = drjson_get_by_index(ctx, v, 1);
    TestAssertNotEqual((int)val.kind, DRJSON_ERROR);
    TestAssert(drjson_eq(q, val));

    drjson_gc(ctx, (DrJsonValue[]){q, val}, 2);
    TestAssert(drjson_eq(q, val));
    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}
TestFunction(TestIntern){
    TESTBEGIN();
    {
        const char* example = "[{hello world} {hello world} {goodbye world} {hello world} {goodbye world} {hello world}]";
        DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
        DrJsonParseContext pctx = {
            .begin = example,
            .cursor = example,
            .end = example + strlen(example),
            .ctx = ctx,
        };
        DrJsonValue v = drjson_parse(&pctx, DRJSON_PARSE_FLAG_NO_COPY_STRINGS|DRJSON_PARSE_FLAG_INTERN_OBJECTS);
        TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
        TestAssertEquals((int)v.kind, DRJSON_ARRAY);

        TestAssertEquals(drjson_len(ctx, v), 6);
        DrJsonValue vs[6];
        for(size_t i = 0; i < arrlen(vs); i++){
            vs[i] = drjson_get_by_index(ctx, v, i);
            TestAssertNotEqual((int)vs[i].kind, DRJSON_ERROR);
        }
        TestExpectTrue(drjson_eq(vs[0], vs[1]));
        TestExpectTrue(drjson_eq(vs[0], vs[3]));
        TestExpectTrue(drjson_eq(vs[0], vs[5]));
        TestExpectTrue(drjson_eq(vs[2], vs[4]));
        TestExpectFalse(drjson_eq(vs[0], vs[2]));

        drjson_gc(ctx, (DrJsonValue[]){vs[0], vs[1]}, 2);
        TestExpectTrue(drjson_eq(vs[0], vs[1]));
        drjson_gc(ctx, 0, 0);
        drjson_ctx_free_all(ctx);
        assert_all_freed();
    }
    {
        const char* example = "["
            "[{hello world} {hello world} {goodbye world} {hello world} {goodbye world} {hello world}]"
            "[{hello world} {hello world} {goodbye world} {hello world} {goodbye world} {hello world}]"
            "[{hello world} {hello world} {goodbye world} {hello world} {goodbye world} {hello world}]"
            "]"
            ;
        DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
        // Loop to trigger bugs in GC
        for(size_t i = 0; i < 10; i++){
            DrJsonParseContext pctx = {
                .begin = example,
                .cursor = example,
                .end = example + strlen(example),
                .ctx = ctx,
            };
            DrJsonValue v = drjson_parse(&pctx, DRJSON_PARSE_FLAG_NO_COPY_STRINGS|DRJSON_PARSE_FLAG_INTERN_OBJECTS);
            TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
            TestAssertEquals((int)v.kind, DRJSON_ARRAY);
            TestAssertEquals(drjson_len(ctx, v), 3);
            DrJsonValue outer[3] = {0};
            DrJsonValue inner[3][6] = {0};
            for(size_t i = 0; i < 3; i++){
                outer[i] = drjson_get_by_index(ctx, v, i);
                TestAssertNotEqual((int)outer[i].kind, DRJSON_ERROR);
                TestAssertEquals((int)outer[i].kind, DRJSON_ARRAY);
                TestAssertEquals(drjson_len(ctx, outer[i]), 6);
                for(size_t j = 0; j < 6; j++){
                    inner[i][j] = drjson_get_by_index(ctx, outer[i], j);
                    TestAssertNotEqual((int)inner[i][j].kind, DRJSON_ERROR);
                    TestAssertEquals((int)inner[i][j].kind, DRJSON_OBJECT);
                }
            }
            TestExpectTrue(drjson_eq(outer[0], outer[1]));
            TestExpectTrue(drjson_eq(outer[0], outer[2]));
            for(size_t i = 0; i < 3; i++){
                TestExpectTrue(drjson_eq(inner[i][0], inner[i][1]));
                TestExpectTrue(drjson_eq(inner[i][0], inner[i][3]));
                TestExpectTrue(drjson_eq(inner[i][0], inner[i][5]));
                TestExpectTrue(drjson_eq(inner[i][2], inner[i][4]));
                TestExpectFalse(drjson_eq(inner[i][0], inner[i][2]));
            }
            DrJsonValue o = drjson_make_object(ctx);
            TestAssertEquals((int)o.kind, DRJSON_OBJECT);
            DrJsonValue world = drjson_make_string(ctx, "world", 5);
            TestAssertEquals((int)world.kind, DRJSON_STRING);
            int e = drjson_object_set_item_no_copy_key(ctx, o, "hello", 5, world);
            TestAssertFalse(e);
            DrJsonValue o2 = drjson_intern_value(ctx, o, 0);
            TestAssertEquals((int)o2.kind, DRJSON_OBJECT);
            TestExpectFalse(drjson_eq(o, o2));
            TestExpectTrue(drjson_eq(inner[0][0], o2));
            o = drjson_intern_value(ctx, o, 1);
            TestAssertEquals((int)o.kind, DRJSON_OBJECT);
            TestExpectTrue(drjson_eq(o, o2));
            TestExpectTrue(drjson_eq(inner[0][0], o));

            drjson_gc(ctx, outer, 3);
            drjson_gc(ctx, 0, 0);
        }
        drjson_ctx_free_all(ctx);
        assert_all_freed();
    }
    TESTEND();
}

TestFunction(TestDoubleParsing){
    TESTBEGIN();
    struct {
        const char* example;
        double value;
    }cases[] = {
#define T(x) {#x, x}
    T(12.), T(12.e12), T(12128123.12),
    T(-0.), T(0.), T(12182312837182371.),
    T(.111), T(0.1),T(1.),
    };
    for(size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++){
        const char* example = cases[i].example;
        DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
        DrJsonValue v = drjson_parse_string(ctx, example, strlen(example), DRJSON_PARSE_FLAG_NO_COPY_STRINGS);
        TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
        TestAssertEquals((int)v.kind, DRJSON_NUMBER);
        TestAssertEquals(v.number, cases[i].value);
        drjson_ctx_free_all(ctx);
        assert_all_freed();
    }
    TESTEND();
}

static inline
_Bool
str_eq(const char* a, const char* b){
    return strcmp(a, b) == 0;
}

TestFunction(TestSerialization){
    TESTBEGIN();
    const char* example = "{foo {bar {bazinga 3}}}";
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    DrJsonValue v = drjson_parse_string(ctx, example, strlen(example), 0);
    TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
    char buff[512];
    size_t printed;
    int err = drjson_print_value_mem(ctx, buff, sizeof buff, v, 0, DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed <= sizeof buff);
    TestAssert(printed);
    TestAssertEquals(buff[printed-1], '\0');
    TestAssertEquals2(str_eq, buff, "{\"foo\":{\"bar\":{\"bazinga\":3}}}");
    drjson_gc(ctx, (DrJsonValue[]){v}, 1);
    err = drjson_print_value_mem(ctx, buff, sizeof buff, v, 0, DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed <= sizeof buff);
    TestAssert(printed);
    TestAssertEquals(buff[printed-1], '\0');
    TestAssertEquals2(str_eq, buff, "{\"foo\":{\"bar\":{\"bazinga\":3}}}");
    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestPrettyPrint){
    TESTBEGIN();
    const char* example = "{foo 0x3}";
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    DrJsonValue v = drjson_parse_string(ctx, example, strlen(example), 0);
    TestAssertNotEqual((int)v.kind, DRJSON_ERROR);
    char buff[512];
    size_t printed;
    int err = drjson_print_value_mem(ctx, buff, sizeof buff, v, 0, DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed <= sizeof buff);
    TestAssert(printed);
    TestAssertEquals(buff[printed-1], '\0');
    TestAssertEquals2(str_eq, buff, "{\"foo\":3}");
    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestEscape){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    struct {
        StringView before, after;
    } test_cases[] = {
        {SV("\r\thello"), SV("\\r\\thello")},
        // Is this right? can you just leave it as utf-8?
        {SV("\u2098hello"), SV("\u2098hello")},
        {SV("\""), SV("\\\"")},
    };
    for(size_t i = 0; i < arrlen(test_cases); i++){
        StringView before = test_cases[i].before;
        StringView after = test_cases[i].after;
        DrJsonAtom a;
        int err = drjson_escape_string(ctx, before.text, before.length, &a);
        TestAssertFalse(err);
        StringView escaped;
        err = drjson_get_atom_str_and_length(ctx, a, &escaped.text, &escaped.length);
        TestAssertFalse(err);
        TestExpectEquals2(SV_equals, escaped, after);
    }
    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestObject){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    size_t count = 0;
    DrJsonValue o = drjson_make_object(ctx);
    TestAssertEquals(o.kind, DRJSON_OBJECT);
    for(size_t x = 0; x < 256; x++){
        for(size_t y = 0; y < 256; y++){
            unsigned char txt[2] = {(unsigned char)x, (unsigned char)y};
            DrJsonValue v = drjson_make_string(ctx, (char*)txt, sizeof txt);
            TestAssertEquals(v.kind, DRJSON_STRING);
            int err = drjson_object_set_item_atom(ctx, o, v.atom, v);
            TestAssertFalse(err);
            count++;
            TestAssertEquals(drjson_len(ctx, o), count);
            DrJsonValue v2 = drjson_object_get_item_atom(ctx, o, v.atom);
            TestAssertEquals(v.kind, v2.kind);
            TestAssertEquals(v.atom.bits, v2.atom.bits);
        }
    }
    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestPathParse){
    TESTBEGIN();
    const char* example = "{ \"a\": { \"b\": [1, 2, 3] }, \"c\": 4 }";
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    DrJsonValue root = drjson_parse_string(ctx, example, strlen(example), 0);
    TestAssertEquals(root.kind, DRJSON_OBJECT);

    DrJsonPath path;
    const char* path_str = "a.b[1]";
    int err = drjson_path_parse(ctx, path_str, strlen(path_str), &path);
    TestAssertFalse(err);

    TestAssertEquals(path.count, 3);
    TestAssertEquals(path.segments[0].kind, DRJSON_PATH_KEY);
    TestAssertEquals(path.segments[1].kind, DRJSON_PATH_KEY);
    TestAssertEquals(path.segments[2].kind, DRJSON_PATH_INDEX);
    TestAssertEquals(path.segments[2].index, 1);

    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#ifdef DRJSON_UNITY
// #define DRJ_DEBUG
#include "drjson.c"
#endif
