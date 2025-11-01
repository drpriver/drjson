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
static TestFunc TestObjectDeletion;

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
    RegisterTest(TestObjectDeletion);
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

TestFunction(TestObjectDeletion){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test 1: Basic deletion and order preservation
    {
        DrJsonValue obj = drjson_make_object(ctx);
        TestAssertEquals(obj.kind, DRJSON_OBJECT);

        DrJsonAtom key_a, key_b, key_c, key_d;
        int err;
        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "b", 1, &key_b);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "c", 1, &key_c);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "d", 1, &key_d);
        TestAssertFalse(err);

        err = drjson_object_set_item_atom(ctx, obj, key_a, drjson_make_int(1));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_b, drjson_make_int(2));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_c, drjson_make_int(3));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_d, drjson_make_int(4));
        TestAssertFalse(err);

        int64_t len = drjson_len(ctx, obj);
        TestAssertEquals(len, 4);

        // Delete key "b" (middle element)
        err = drjson_object_delete_item_atom(ctx, obj, key_b);
        TestAssertEquals(err, 0); // 0 = success

        len = drjson_len(ctx, obj);
        TestAssertEquals(len, 3);

        // Verify key "b" is gone
        DrJsonValue val_b = drjson_object_get_item_atom(ctx, obj, key_b);
        TestAssertEquals(val_b.kind, DRJSON_ERROR);

        // Verify other keys still exist and have correct values
        DrJsonValue val_a = drjson_object_get_item_atom(ctx, obj, key_a);
        TestAssertEquals(val_a.kind, DRJSON_INTEGER);
        TestAssertEquals(val_a.integer, 1);

        DrJsonValue val_c = drjson_object_get_item_atom(ctx, obj, key_c);
        TestAssertEquals(val_c.kind, DRJSON_INTEGER);
        TestAssertEquals(val_c.integer, 3);

        DrJsonValue val_d = drjson_object_get_item_atom(ctx, obj, key_d);
        TestAssertEquals(val_d.kind, DRJSON_INTEGER);
        TestAssertEquals(val_d.integer, 4);

        // Test insertion order preservation via keys()
        DrJsonValue keys = drjson_object_keys(obj);
        int64_t keys_len = drjson_len(ctx, keys);
        TestAssertEquals(keys_len, 3);

        // Should be: a, c, d (b was deleted, order preserved)
        DrJsonValue key0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(key0.atom.bits, key_a.bits);

        DrJsonValue key1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(key1.atom.bits, key_c.bits);

        DrJsonValue key2 = drjson_get_by_index(ctx, keys, 2);
        TestAssertEquals(key2.atom.bits, key_d.bits);

        // Test order preservation via values()
        DrJsonValue values = drjson_object_values(obj);
        int64_t values_len = drjson_len(ctx, values);
        TestAssertEquals(values_len, 3);

        DrJsonValue val0 = drjson_get_by_index(ctx, values, 0);
        TestAssertEquals(val0.integer, 1);

        DrJsonValue val1 = drjson_get_by_index(ctx, values, 1);
        TestAssertEquals(val1.integer, 3);

        DrJsonValue val2 = drjson_get_by_index(ctx, values, 2);
        TestAssertEquals(val2.integer, 4);

        // Test order preservation via items()
        DrJsonValue items = drjson_object_items(obj);
        int64_t items_len = drjson_len(ctx, items);
        TestAssertEquals(items_len / 2, 3); // 3 keys, 6 items total

        DrJsonValue item_key0 = drjson_get_by_index(ctx, items, 0);
        TestAssertEquals(item_key0.atom.bits, key_a.bits);
        DrJsonValue item_val0 = drjson_get_by_index(ctx, items, 1);
        TestAssertEquals(item_val0.integer, 1);

        DrJsonValue item_key1 = drjson_get_by_index(ctx, items, 2);
        TestAssertEquals(item_key1.atom.bits, key_c.bits);
        DrJsonValue item_val1 = drjson_get_by_index(ctx, items, 3);
        TestAssertEquals(item_val1.integer, 3);

        DrJsonValue item_key2 = drjson_get_by_index(ctx, items, 4);
        TestAssertEquals(item_key2.atom.bits, key_d.bits);
        DrJsonValue item_val2 = drjson_get_by_index(ctx, items, 5);
        TestAssertEquals(item_val2.integer, 4);

        // Test deleting non-existent key
        err = drjson_object_delete_item_atom(ctx, obj, key_b);
        TestAssertEquals(err, 1); // 1 = not found

        // Test deleting first element
        err = drjson_object_delete_item_atom(ctx, obj, key_a);
        TestAssertFalse(err);
        len = drjson_len(ctx, obj);
        TestAssertEquals(len, 2);

        // Verify order after first deletion: should be c, d
        keys = drjson_object_keys(obj);
        DrJsonValue key_after0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(key_after0.atom.bits, key_c.bits);
        DrJsonValue key_after1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(key_after1.atom.bits, key_d.bits);

        // Test deleting last element
        err = drjson_object_delete_item_atom(ctx, obj, key_d);
        TestAssertFalse(err);
        len = drjson_len(ctx, obj);
        TestAssertEquals(len, 1);

        // Only 'c' should remain
        DrJsonValue val_c_final = drjson_object_get_item_atom(ctx, obj, key_c);
        TestAssertEquals(val_c_final.kind, DRJSON_INTEGER);
        TestAssertEquals(val_c_final.integer, 3);

        keys = drjson_object_keys(obj);
        DrJsonValue last_key = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(last_key.atom.bits, key_c.bits);

        // Delete the last element
        err = drjson_object_delete_item_atom(ctx, obj, key_c);
        TestAssertFalse(err);
        len = drjson_len(ctx, obj);
        TestAssertEquals(len, 0);
    }

    // Test 2: Deletion with string API
    {
        DrJsonValue obj2 = drjson_make_object(ctx);
        int err = drjson_object_set_item_copy_key(ctx, obj2, "foo", 3, drjson_make_int(42));
        TestAssertFalse(err);
        err = drjson_object_set_item_copy_key(ctx, obj2, "bar", 3, drjson_make_int(99));
        TestAssertFalse(err);

        err = drjson_object_delete_item(ctx, obj2, "foo", 3);
        TestAssertFalse(err);

        DrJsonValue val_bar = drjson_object_get_item(ctx, obj2, "bar", 3);
        TestAssertEquals(val_bar.kind, DRJSON_INTEGER);
        TestAssertEquals(val_bar.integer, 99);

        DrJsonValue val_foo = drjson_object_get_item(ctx, obj2, "foo", 3);
        TestAssertEquals(val_foo.kind, DRJSON_ERROR);
    }

    // Test 3: Large object with resizing
    // Objects start with capacity 4, then double: 4 -> 8 -> 16 -> 32 -> 64
    {
        DrJsonValue obj3 = drjson_make_object(ctx);

        // Add 100 keys to force multiple resizes
        DrJsonAtom keys[100];
        for(int i = 0; i < 100; i++){
            char keybuf[32];
            int keylen = snprintf(keybuf, sizeof(keybuf), "key_%d", i);
            int err = drjson_atomize(ctx, keybuf, (size_t)keylen, &keys[i]);
            TestAssertFalse(err);
            err = drjson_object_set_item_atom(ctx, obj3, keys[i], drjson_make_int(i * 10));
            TestAssertFalse(err);
        }

        int64_t len = drjson_len(ctx, obj3);
        TestAssertEquals(len, 100);

        // Verify order is preserved (all 100 keys in insertion order)
        DrJsonValue obj3_keys = drjson_object_keys(obj3);
        for(int i = 0; i < 100; i++){
            DrJsonValue key = drjson_get_by_index(ctx, obj3_keys, i);
            TestAssertEquals(key.atom.bits, keys[i].bits);
        }

        // Delete every 3rd key (keys 0, 3, 6, 9, ...)
        for(int i = 0; i < 100; i += 3){
            int err = drjson_object_delete_item_atom(ctx, obj3, keys[i]);
            TestAssertEquals(err, 0);
        }

        len = drjson_len(ctx, obj3);
        TestAssertEquals(len, 66); // 100 - 34 = 66

        // Verify remaining keys are still in order
        obj3_keys = drjson_object_keys(obj3);
        int expected_idx = 1; // Start at 1 since 0 was deleted
        for(int i = 0; i < 66; i++){
            // Skip deleted keys (0, 3, 6, ...)
            while(expected_idx % 3 == 0) expected_idx++;

            DrJsonValue key = drjson_get_by_index(ctx, obj3_keys, i);
            TestAssertEquals(key.atom.bits, keys[expected_idx].bits);

            // Verify value is correct
            DrJsonValue val = drjson_object_get_item_atom(ctx, obj3, keys[expected_idx]);
            TestAssertEquals(val.kind, DRJSON_INTEGER);
            TestAssertEquals(val.integer, expected_idx * 10);

            expected_idx++;
        }

        // Delete every other remaining key
        // Save the atoms first since the keys view will change as we delete
        DrJsonAtom keys_to_delete[33];
        obj3_keys = drjson_object_keys(obj3);
        for(int i = 0; i < 66; i += 2){
            DrJsonValue key = drjson_get_by_index(ctx, obj3_keys, i);
            keys_to_delete[i/2] = key.atom;
        }
        for(int i = 0; i < 33; i++){
            int err = drjson_object_delete_item_atom(ctx, obj3, keys_to_delete[i]);
            TestAssertEquals(err, 0);
        }

        len = drjson_len(ctx, obj3);
        TestAssertEquals(len, 33); // 66 - 33 = 33

        // Verify order is still preserved
        obj3_keys = drjson_object_keys(obj3);
        for(int i = 0; i < 33; i++){
            DrJsonValue key_i = drjson_get_by_index(ctx, obj3_keys, i);

            // Verify we can look it up
            DrJsonValue val = drjson_object_get_item_atom(ctx, obj3, key_i.atom);
            TestAssertEquals(val.kind, DRJSON_INTEGER);
        }

        // Add 50 more keys to test adding after deletions
        DrJsonAtom new_keys[50];
        for(int i = 0; i < 50; i++){
            char keybuf[32];
            int keylen = snprintf(keybuf, sizeof(keybuf), "new_key_%d", i);
            int err = drjson_atomize(ctx, keybuf, (size_t)keylen, &new_keys[i]);
            TestAssertFalse(err);
            err = drjson_object_set_item_atom(ctx, obj3, new_keys[i], drjson_make_int(i * 100));
            TestAssertFalse(err);
        }

        len = drjson_len(ctx, obj3);
        TestAssertEquals(len, 83); // 33 + 50 = 83

        // Verify new keys are at the end in order
        obj3_keys = drjson_object_keys(obj3);
        for(int i = 0; i < 50; i++){
            DrJsonValue key = drjson_get_by_index(ctx, obj3_keys, 33 + i);
            TestAssertEquals(key.atom.bits, new_keys[i].bits);

            DrJsonValue val = drjson_object_get_item_atom(ctx, obj3, new_keys[i]);
            TestAssertEquals(val.kind, DRJSON_INTEGER);
            TestAssertEquals(val.integer, i * 100);
        }
    }

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
