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
#include "test_allocator.h"
#include "../compiler_warnings.h"

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif
static TestFunc TestSimpleParsing;
static TestFunc TestSimpleParsing2;
static TestFunc TestIntern;
static TestFunc TestDoubleParsing;
static TestFunc TestSerialization;
static TestFunc TestPrettyPrint;
static TestFunc TestBracelessPrint;
static TestFunc TestTrailingContent;
static TestFunc TestEscape;
static TestFunc TestObject;
static TestFunc TestPathParse;
static TestFunc TestObjectDeletion;
static TestFunc TestObjectReplaceKey;
static TestFunc TestObjectInsertAtIndex;
static TestFunc TestArrayManipulation;
static TestFunc TestErrorUtilities;
static TestFunc TestPrintToFd;
static TestFunc TestFloatPrinting;
static TestFunc TestCheckedQuery;
static TestFunc TestClearValue;
static TestFunc TestArraySwap;
static TestFunc TestArrayMove;
static TestFunc TestObjectMove;

int main(int argc, char*_Nullable*_Nonnull argv){
    RegisterTest(TestSimpleParsing);
    RegisterTest(TestSimpleParsing2);
    RegisterTest(TestIntern);
    RegisterTest(TestDoubleParsing);
    RegisterTest(TestSerialization);
    RegisterTest(TestPrettyPrint);
    RegisterTest(TestBracelessPrint);
    RegisterTest(TestTrailingContent);
    RegisterTest(TestEscape);
    RegisterTest(TestObject);
    RegisterTest(TestPathParse);
    RegisterTest(TestObjectDeletion);
    RegisterTest(TestObjectReplaceKey);
    RegisterTest(TestObjectInsertAtIndex);
    RegisterTest(TestArrayManipulation);
    RegisterTest(TestErrorUtilities);
    RegisterTest(TestPrintToFd);
    RegisterTest(TestFloatPrinting);
    RegisterTest(TestCheckedQuery);
    RegisterTest(TestClearValue);
    RegisterTest(TestArraySwap);
    RegisterTest(TestArrayMove);
    RegisterTest(TestObjectMove);
    return test_main(argc, argv, NULL);
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

TestFunction(TestBracelessPrint){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Create an object {"name": "Alice", "age": 30}
    DrJsonValue obj = drjson_make_object(ctx);
    DrJsonAtom name, age;
    int err = drjson_atomize(ctx, "name", 4, &name);
    TestAssertFalse(err);
    err = drjson_atomize(ctx, "age", 3, &age);
    TestAssertFalse(err);
    err = drjson_object_set_item_atom(ctx, obj, name, drjson_make_string(ctx, "Alice", 5));
    TestAssertFalse(err);
    err = drjson_object_set_item_atom(ctx, obj, age, drjson_make_int(30));
    TestAssertFalse(err);

    // Test braceless printing (compact)
    char buff[512];
    size_t printed;
    err = drjson_print_value_mem(ctx, buff, sizeof buff, obj, 0, DRJSON_PRINT_BRACELESS | DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed <= sizeof buff);
    TestAssert(printed);
    TestAssertEquals(buff[printed-1], '\0');

    // Check exact output
    StringView actual = {.length = printed - 1, .text = buff};
    TestExpectEquals2(SV_equals, actual, SV("\"name\":\"Alice\",\"age\":30"));

    // Test round-trip: parse braceless and reserialize normally
    DrJsonValue reparsed = drjson_parse_string(ctx, buff, printed - 1, DRJSON_PARSE_FLAG_BRACELESS_OBJECT);
    TestAssertNotEqual((int)reparsed.kind, DRJSON_ERROR);
    TestAssertEquals((int)reparsed.kind, DRJSON_OBJECT);

    // Verify the values match
    DrJsonValue name_val = drjson_object_get_item_atom(ctx, reparsed, name);
    TestAssertEquals((int)name_val.kind, DRJSON_STRING);
    DrJsonValue age_val = drjson_object_get_item_atom(ctx, reparsed, age);
    TestAssert(drjson_is_numeric(age_val));
    // Could be INTEGER or UINTEGER depending on parser
    if(age_val.kind == DRJSON_INTEGER)
        TestAssertEquals(age_val.integer, 30);
    else
        TestAssertEquals(age_val.uinteger, 30);

    // Print normally and verify it has braces
    char buff2[512];
    err = drjson_print_value_mem(ctx, buff2, sizeof buff2, reparsed, 0, DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssertEquals(buff2[0], '{');
    TestAssertEquals(buff2[printed-2], '}');

    // Test braceless with pretty print
    char buff3[512];
    err = drjson_print_value_mem(ctx, buff3, sizeof buff3, obj, 0, DRJSON_PRINT_BRACELESS | DRJSON_PRETTY_PRINT | DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed <= sizeof buff3);
    TestAssert(printed);
    TestAssertEquals(buff3[printed-1], '\0');

    // Check exact output
    StringView actual_pretty = {.length = printed - 1, .text = buff3};
    TestExpectEquals2(SV_equals, actual_pretty, SV("\"name\": \"Alice\",\n\"age\": 30"));

    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestTrailingContent){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test 1: Parse with trailing content without flag should succeed
    {
        const char* json = "\"hello\" \"world\"";
        DrJsonValue result = drjson_parse_string(ctx, json, strlen(json), 0);
        TestAssertEquals((int)result.kind, DRJSON_STRING);
    }

    // Test 2: Parse with trailing content with flag should error
    {
        const char* json = "\"hello\" \"world\"";
        DrJsonValue result = drjson_parse_string(ctx, json, strlen(json), DRJSON_PARSE_FLAG_ERROR_ON_TRAILING);
        TestAssertEquals((int)result.kind, DRJSON_ERROR);
        TestAssertEquals(result.error_code, DRJSON_ERROR_TRAILING_CONTENT);
    }

    // Test 3: Parse without trailing content with flag should succeed
    {
        const char* json = "{\"name\": \"Alice\"}";
        DrJsonValue result = drjson_parse_string(ctx, json, strlen(json), DRJSON_PARSE_FLAG_ERROR_ON_TRAILING);
        TestAssertEquals((int)result.kind, DRJSON_OBJECT);
    }

    // Test 4: Parse with trailing whitespace with flag should succeed
    {
        const char* json = "{\"name\": \"Alice\"}   \n  ";
        DrJsonValue result = drjson_parse_string(ctx, json, strlen(json), DRJSON_PARSE_FLAG_ERROR_ON_TRAILING);
        TestAssertEquals((int)result.kind, DRJSON_OBJECT);
    }

    // Test 5: Array with trailing comma/content should error
    {
        const char* json = "[1, 2, 3], 4";
        DrJsonValue result = drjson_parse_string(ctx, json, strlen(json), DRJSON_PARSE_FLAG_ERROR_ON_TRAILING);
        TestAssertEquals((int)result.kind, DRJSON_ERROR);
        TestAssertEquals(result.error_code, DRJSON_ERROR_TRAILING_CONTENT);
    }

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

TestFunction(TestObjectReplaceKey){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test 1: Basic key replacement
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a, key_b, key_c, key_new;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "b", 1, &key_b);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "c", 1, &key_c);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "new_key", 7, &key_new);
        TestAssertFalse(err);

        err = drjson_object_set_item_atom(ctx, obj, key_a, drjson_make_int(1));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_b, drjson_make_int(2));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_c, drjson_make_int(3));
        TestAssertFalse(err);

        // Replace key "b" with "new_key"
        err = drjson_object_replace_key_atom(ctx, obj, key_b, key_new);
        TestAssertEquals(err, 0);

        // Verify old key doesn't exist
        DrJsonValue val_b = drjson_object_get_item_atom(ctx, obj, key_b);
        TestAssertEquals(val_b.kind, DRJSON_ERROR);

        // Verify new key exists with the correct value
        DrJsonValue val_new = drjson_object_get_item_atom(ctx, obj, key_new);
        TestAssertEquals(val_new.kind, DRJSON_INTEGER);
        TestAssertEquals(val_new.integer, 2);

        // Verify other keys still exist
        DrJsonValue val_a = drjson_object_get_item_atom(ctx, obj, key_a);
        TestAssertEquals(val_a.kind, DRJSON_INTEGER);
        TestAssertEquals(val_a.integer, 1);

        DrJsonValue val_c = drjson_object_get_item_atom(ctx, obj, key_c);
        TestAssertEquals(val_c.kind, DRJSON_INTEGER);
        TestAssertEquals(val_c.integer, 3);

        // Verify insertion order is preserved
        DrJsonValue keys = drjson_object_keys(obj);
        int64_t keys_len = drjson_len(ctx, keys);
        TestAssertEquals(keys_len, 3);

        // Should be: a, new_key, c (b was replaced with new_key in middle position)
        DrJsonValue key0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(key0.atom.bits, key_a.bits);

        DrJsonValue key1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(key1.atom.bits, key_new.bits);

        DrJsonValue key2 = drjson_get_by_index(ctx, keys, 2);
        TestAssertEquals(key2.atom.bits, key_c.bits);
    }

    // Test 2: Replace first and last keys
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key1, key2, key3, key_first, key_last;
        int err;

        err = drjson_atomize(ctx, "first", 5, &key1);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "middle", 6, &key2);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "last", 4, &key3);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "new_first", 9, &key_first);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "new_last", 8, &key_last);
        TestAssertFalse(err);

        err = drjson_object_set_item_atom(ctx, obj, key1, drjson_make_int(10));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key2, drjson_make_int(20));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key3, drjson_make_int(30));
        TestAssertFalse(err);

        // Replace first key
        err = drjson_object_replace_key_atom(ctx, obj, key1, key_first);
        TestAssertEquals(err, 0);

        // Replace last key
        err = drjson_object_replace_key_atom(ctx, obj, key3, key_last);
        TestAssertEquals(err, 0);

        // Verify order: new_first, middle, new_last
        DrJsonValue keys = drjson_object_keys(obj);
        DrJsonValue k0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(k0.atom.bits, key_first.bits);

        DrJsonValue k1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(k1.atom.bits, key2.bits);

        DrJsonValue k2 = drjson_get_by_index(ctx, keys, 2);
        TestAssertEquals(k2.atom.bits, key_last.bits);

        // Verify values are correct
        DrJsonValue v0 = drjson_object_get_item_atom(ctx, obj, key_first);
        TestAssertEquals(v0.integer, 10);

        DrJsonValue v1 = drjson_object_get_item_atom(ctx, obj, key2);
        TestAssertEquals(v1.integer, 20);

        DrJsonValue v2 = drjson_object_get_item_atom(ctx, obj, key_last);
        TestAssertEquals(v2.integer, 30);
    }

    // Test 3: Replace non-existent key should fail
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_exists, key_missing, key_new;
        int err;

        err = drjson_atomize(ctx, "exists", 6, &key_exists);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "missing", 7, &key_missing);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "new", 3, &key_new);
        TestAssertFalse(err);

        err = drjson_object_set_item_atom(ctx, obj, key_exists, drjson_make_int(42));
        TestAssertFalse(err);

        // Try to replace non-existent key
        err = drjson_object_replace_key_atom(ctx, obj, key_missing, key_new);
        TestAssertEquals(err, 1); // Should fail

        // Original key should still exist
        DrJsonValue val = drjson_object_get_item_atom(ctx, obj, key_exists);
        TestAssertEquals(val.kind, DRJSON_INTEGER);
        TestAssertEquals(val.integer, 42);
    }

    // Test 4: Replacing with a key that already exists should fail (duplicate prevention)
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a, key_b, key_c;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "b", 1, &key_b);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "c", 1, &key_c);
        TestAssertFalse(err);

        err = drjson_object_set_item_atom(ctx, obj, key_a, drjson_make_int(1));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_b, drjson_make_int(2));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_c, drjson_make_int(3));
        TestAssertFalse(err);

        // Try to rename "a" to "b" (but "b" already exists)
        err = drjson_object_replace_key_atom(ctx, obj, key_a, key_b);
        TestAssertEquals(err, 1); // Should fail - would create duplicate

        // Verify object is unchanged
        int64_t len = drjson_len(ctx, obj);
        TestAssertEquals(len, 3);

        DrJsonValue val_a = drjson_object_get_item_atom(ctx, obj, key_a);
        TestAssertEquals(val_a.kind, DRJSON_INTEGER);
        TestAssertEquals(val_a.integer, 1);

        DrJsonValue val_b = drjson_object_get_item_atom(ctx, obj, key_b);
        TestAssertEquals(val_b.kind, DRJSON_INTEGER);
        TestAssertEquals(val_b.integer, 2);

        DrJsonValue val_c = drjson_object_get_item_atom(ctx, obj, key_c);
        TestAssertEquals(val_c.kind, DRJSON_INTEGER);
        TestAssertEquals(val_c.integer, 3);

        // Verify order is still a, b, c
        DrJsonValue keys = drjson_object_keys(obj);
        DrJsonValue k0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(k0.atom.bits, key_a.bits);
        DrJsonValue k1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(k1.atom.bits, key_b.bits);
        DrJsonValue k2 = drjson_get_by_index(ctx, keys, 2);
        TestAssertEquals(k2.atom.bits, key_c.bits);
    }

    // Test 5: Replacing a key with itself should succeed (no-op)
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_a, drjson_make_int(42));
        TestAssertFalse(err);

        // Replace "a" with "a" (same key)
        err = drjson_object_replace_key_atom(ctx, obj, key_a, key_a);
        TestAssertEquals(err, 0); // Should succeed

        // Verify value is unchanged
        DrJsonValue val = drjson_object_get_item_atom(ctx, obj, key_a);
        TestAssertEquals(val.kind, DRJSON_INTEGER);
        TestAssertEquals(val.integer, 42);
    }

    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestObjectInsertAtIndex){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test 1: Insert at beginning of empty object
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);

        err = drjson_object_insert_item_at_index(ctx, obj, key_a, drjson_make_int(1), 0);
        TestAssertEquals(err, 0);

        DrJsonValue val = drjson_object_get_item_atom(ctx, obj, key_a);
        TestAssertEquals(val.kind, DRJSON_INTEGER);
        TestAssertEquals(val.integer, 1);
    }

    // Test 2: Insert at specific positions to control order
    {
        DrJsonValue obj = drjson_make_object(ctx);
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

        // Insert at end (index 0 in empty object)
        err = drjson_object_insert_item_at_index(ctx, obj, key_a, drjson_make_int(1), 0);
        TestAssertEquals(err, 0);

        // Insert at end (index 1)
        err = drjson_object_insert_item_at_index(ctx, obj, key_c, drjson_make_int(3), 1);
        TestAssertEquals(err, 0);

        // Insert in middle (index 1, shifting "c" to index 2)
        err = drjson_object_insert_item_at_index(ctx, obj, key_b, drjson_make_int(2), 1);
        TestAssertEquals(err, 0);

        // Insert at end (index 3)
        err = drjson_object_insert_item_at_index(ctx, obj, key_d, drjson_make_int(4), 3);
        TestAssertEquals(err, 0);

        // Verify order is a, b, c, d
        DrJsonValue keys = drjson_object_keys(obj);
        TestAssertEquals(drjson_len(ctx, keys), 4);

        DrJsonValue k0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(k0.atom.bits, key_a.bits);
        DrJsonValue k1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(k1.atom.bits, key_b.bits);
        DrJsonValue k2 = drjson_get_by_index(ctx, keys, 2);
        TestAssertEquals(k2.atom.bits, key_c.bits);
        DrJsonValue k3 = drjson_get_by_index(ctx, keys, 3);
        TestAssertEquals(k3.atom.bits, key_d.bits);
    }

    // Test 3: Cannot insert duplicate key
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);

        err = drjson_object_insert_item_at_index(ctx, obj, key_a, drjson_make_int(1), 0);
        TestAssertEquals(err, 0);

        // Try to insert the same key again
        err = drjson_object_insert_item_at_index(ctx, obj, key_a, drjson_make_int(2), 0);
        TestAssertEquals(err, 1); // Should fail

        // Verify value is unchanged
        DrJsonValue val = drjson_object_get_item_atom(ctx, obj, key_a);
        TestAssertEquals(val.integer, 1);
    }

    // Test 4: Cannot insert at invalid index
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a, key_b;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "b", 1, &key_b);
        TestAssertFalse(err);

        err = drjson_object_insert_item_at_index(ctx, obj, key_a, drjson_make_int(1), 0);
        TestAssertEquals(err, 0);

        // Try to insert at index 2 (count is 1, so valid indices are 0-1)
        err = drjson_object_insert_item_at_index(ctx, obj, key_b, drjson_make_int(2), 2);
        TestAssertEquals(err, 1); // Should fail

        // Inserting at index 1 (count) should succeed (append)
        err = drjson_object_insert_item_at_index(ctx, obj, key_b, drjson_make_int(2), 1);
        TestAssertEquals(err, 0); // Should succeed
    }

    // Test 5: Insert at beginning shifts existing items
    {
        DrJsonValue obj = drjson_make_object(ctx);
        DrJsonAtom key_a, key_b, key_z;
        int err;

        err = drjson_atomize(ctx, "a", 1, &key_a);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "b", 1, &key_b);
        TestAssertFalse(err);
        err = drjson_atomize(ctx, "z", 1, &key_z);
        TestAssertFalse(err);

        // Add a and b normally
        err = drjson_object_set_item_atom(ctx, obj, key_a, drjson_make_int(1));
        TestAssertFalse(err);
        err = drjson_object_set_item_atom(ctx, obj, key_b, drjson_make_int(2));
        TestAssertFalse(err);

        // Insert z at beginning
        err = drjson_object_insert_item_at_index(ctx, obj, key_z, drjson_make_int(26), 0);
        TestAssertEquals(err, 0);

        // Verify order is z, a, b
        DrJsonValue keys = drjson_object_keys(obj);
        DrJsonValue k0 = drjson_get_by_index(ctx, keys, 0);
        TestAssertEquals(k0.atom.bits, key_z.bits);
        DrJsonValue k1 = drjson_get_by_index(ctx, keys, 1);
        TestAssertEquals(k1.atom.bits, key_a.bits);
        DrJsonValue k2 = drjson_get_by_index(ctx, keys, 2);
        TestAssertEquals(k2.atom.bits, key_b.bits);
    }

    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestArrayManipulation){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test array_push_item (already covered, but verify)
    DrJsonValue arr = drjson_make_array(ctx);
    TestAssertEquals(arr.kind, DRJSON_ARRAY);

    DrJsonValue v1 = drjson_make_int(10);
    DrJsonValue v2 = drjson_make_int(20);
    DrJsonValue v3 = drjson_make_int(30);

    int err = drjson_array_push_item(ctx, arr, v1);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 1);

    err = drjson_array_push_item(ctx, arr, v2);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 2);

    err = drjson_array_push_item(ctx, arr, v3);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 3);

    // Test array_insert_item - insert at beginning
    DrJsonValue v0 = drjson_make_int(5);
    err = drjson_array_insert_item(ctx, arr, 0, v0);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 4);

    DrJsonValue check = drjson_get_by_index(ctx, arr, 0);
    TestAssertEquals(check.integer, 5);
    check = drjson_get_by_index(ctx, arr, 1);
    TestAssertEquals(check.integer, 10);

    // Test array_insert_item - insert in middle
    DrJsonValue v15 = drjson_make_int(15);
    err = drjson_array_insert_item(ctx, arr, 2, v15);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 5);

    check = drjson_get_by_index(ctx, arr, 2);
    TestAssertEquals(check.integer, 15);
    check = drjson_get_by_index(ctx, arr, 3);
    TestAssertEquals(check.integer, 20);

    // Test array_insert_item - insert at end (append)
    DrJsonValue v40 = drjson_make_int(40);
    err = drjson_array_insert_item(ctx, arr, drjson_len(ctx, arr), v40);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 6);

    check = drjson_get_by_index(ctx, arr, 5);
    TestAssertEquals(check.integer, 40);

    // Test array_set_by_index
    DrJsonValue v99 = drjson_make_int(99);
    err = drjson_array_set_by_index(ctx, arr, 2, v99);
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 6); // Length unchanged

    check = drjson_get_by_index(ctx, arr, 2);
    TestAssertEquals(check.integer, 99);

    // Test array_set_by_index with negative index
    DrJsonValue v88 = drjson_make_int(88);
    err = drjson_array_set_by_index(ctx, arr, -1, v88); // Last element
    TestAssertFalse(err);

    check = drjson_get_by_index(ctx, arr, 5);
    TestAssertEquals(check.integer, 88);

    // Test array_pop_item
    DrJsonValue popped = drjson_array_pop_item(ctx, arr);
    TestAssertEquals(popped.kind, DRJSON_INTEGER);
    TestAssertEquals(popped.integer, 88);
    TestAssertEquals(drjson_len(ctx, arr), 5);

    // Test array_del_item - delete from middle
    DrJsonValue deleted = drjson_array_del_item(ctx, arr, 2);
    TestAssertNotEqual(deleted.kind, DRJSON_ERROR);
    TestAssertEquals(drjson_len(ctx, arr), 4);

    // Verify order: [5, 10, 20, 30]
    check = drjson_get_by_index(ctx, arr, 0);
    TestAssertEquals(check.integer, 5);
    check = drjson_get_by_index(ctx, arr, 1);
    TestAssertEquals(check.integer, 10);
    check = drjson_get_by_index(ctx, arr, 2);
    TestAssertEquals(check.integer, 20);
    check = drjson_get_by_index(ctx, arr, 3);
    TestAssertEquals(check.integer, 30);

    // Test array_del_item - delete from beginning
    deleted = drjson_array_del_item(ctx, arr, 0);
    TestAssertNotEqual(deleted.kind, DRJSON_ERROR);
    TestAssertEquals(drjson_len(ctx, arr), 3);

    check = drjson_get_by_index(ctx, arr, 0);
    TestAssertEquals(check.integer, 10);

    // Test error cases - wrong type
    DrJsonValue obj = drjson_make_object(ctx);
    err = drjson_array_insert_item(ctx, obj, 0, v1);
    TestAssert(err != 0);

    DrJsonValue error_result = drjson_array_pop_item(ctx, obj);
    TestAssertEquals(error_result.kind, DRJSON_ERROR);

    error_result = drjson_array_del_item(ctx, obj, 0);
    TestAssertEquals(error_result.kind, DRJSON_ERROR);

    err = drjson_array_set_by_index(ctx, obj, 0, v1);
    TestAssert(err != 0);

    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestErrorUtilities){
    TESTBEGIN();

    // Test drjson_error_name
    size_t len;
    const char* name = drjson_error_name(DRJSON_ERROR_UNEXPECTED_EOF, &len);
    TestAssert(name != NULL);
    TestAssert(len > 0);
    TestAssert(memcmp(name, "Unexpected End of Input", len) == 0);

    name = drjson_error_name(DRJSON_ERROR_TYPE_ERROR, &len);
    TestAssert(name != NULL);
    TestAssert(len > 0);
    TestAssert(memcmp(name, "Invalid type for operation", len) == 0);

    name = drjson_error_name(DRJSON_ERROR_ALLOC_FAILURE, NULL);
    TestAssert(name != NULL);
    TestAssert(memcmp(name, "Allocation Failure", strlen(name)) == 0);

    // Test drjson_kind_name
    name = drjson_kind_name(DRJSON_OBJECT, &len);
    TestAssert(name != NULL);
    TestAssert(len > 0);
    TestAssert(memcmp(name, "object", len) == 0);

    name = drjson_kind_name(DRJSON_ARRAY, &len);
    TestAssert(name != NULL);
    TestAssert(len > 0);
    TestAssert(memcmp(name, "array", len) == 0);

    name = drjson_kind_name(DRJSON_STRING, &len);
    TestAssert(name != NULL);
    TestAssert(len > 0);
    TestAssert(memcmp(name, "string", len) == 0);

    name = drjson_kind_name(DRJSON_INTEGER, NULL);
    TestAssert(name != NULL);
    TestAssert(memcmp(name, "integer", strlen(name)) == 0);

    // Test drjson_get_line_column with parse error
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());
    // String: "{\n  \"foo\": ,\n}"
    // Line 0: "{"
    // Line 1: "  \"foo\": ,"  - positions: 0-1 (spaces), 2 ("), 3-5 (foo), 6 ("), 7 (:), 8 (space), 9 (,)
    const char* invalid_json = "{\n  \"foo\": ,\n}";
    DrJsonParseContext pctx = {
        .begin = invalid_json,
        .cursor = invalid_json,
        .end = invalid_json + strlen(invalid_json),
        .ctx = ctx,
        .depth = 0,
    };
    DrJsonValue v = drjson_parse(&pctx, 0);
    TestAssertEquals(v.kind, DRJSON_ERROR);

    size_t line, column;
    drjson_get_line_column(&pctx, &line, &column);
    // The cursor is at the newline after the comma
    // Line count increments when we encounter '\n', so we're on line 2
    TestAssertEquals(line, 2);
    // Column resets to 0 when we hit the newline
    TestAssertEquals(column, 0);

    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestPrintToFd){
    TESTBEGIN();
#ifndef _WIN32
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Create a test value with multiple types for thorough testing
    DrJsonValue obj = drjson_make_object(ctx);
    int err = drjson_object_set_item_no_copy_key(ctx, obj, "integer", 7, drjson_make_int(42));
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "float", 5, drjson_make_number(3.14));
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "string", 6, drjson_make_string(ctx, "hello", 5));
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "null", 4, drjson_make_null());
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "bool", 4, drjson_make_bool(1));
    TestAssertFalse(err);

    DrJsonValue arr = drjson_make_array(ctx);
    err = drjson_array_push_item(ctx, arr, drjson_make_int(1));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, arr, drjson_make_int(2));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, arr, drjson_make_int(3));
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "array", 5, arr);
    TestAssertFalse(err);

    // Create a temporary file
    char temp_file[] = "/tmp/drjson_test_XXXXXX";
    int fd = mkstemp(temp_file);
    TestAssert(fd >= 0);

    // Test drjson_print_value_fd
    err = drjson_print_value_fd(ctx, fd, obj, 0, 0);
    TestAssertFalse(err);

    // Seek back to beginning to read what we wrote
    off_t seek_result = lseek(fd, 0, SEEK_SET);
    TestAssert(seek_result == 0);

    char buffer[512];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    TestAssert(bytes_read > 0);
    buffer[bytes_read] = '\0';

    close(fd);
    unlink(temp_file);

    // Verify basic format - should be valid JSON object
    TestAssert(buffer[0] == '{');
    TestAssert(buffer[bytes_read - 1] == '}');

    // Round-trip test: parse the output back and verify all fields
    DrJsonValue reparsed = drjson_parse_string(ctx, buffer, bytes_read, 0);
    TestAssertEquals(reparsed.kind, DRJSON_OBJECT);

    // Verify all fields match
    DrJsonValue int_val = drjson_query(ctx, reparsed, "integer", 7);
    // Could be UINTEGER or INTEGER depending on how parser interprets it
    TestAssert(int_val.kind == DRJSON_INTEGER || int_val.kind == DRJSON_UINTEGER);
    if(int_val.kind == DRJSON_INTEGER)
        TestAssertEquals(int_val.integer, 42);
    else
        TestAssertEquals(int_val.uinteger, 42);

    DrJsonValue float_val = drjson_query(ctx, reparsed, "float", 5);
    TestAssertEquals(float_val.kind, DRJSON_NUMBER);
    TestAssertEquals(float_val.number, 3.14);

    DrJsonValue str_val = drjson_query(ctx, reparsed, "string", 6);
    TestAssertEquals(str_val.kind, DRJSON_STRING);
    const char* str; size_t slen;
    err = drjson_get_str_and_len(ctx, str_val, &str, &slen);
    TestAssertFalse(err);
    TestAssertEquals(slen, 5);
    TestAssert(memcmp(str, "hello", 5) == 0);

    DrJsonValue null_val = drjson_query(ctx, reparsed, "null", 4);
    TestAssertEquals(null_val.kind, DRJSON_NULL);

    DrJsonValue bool_val = drjson_query(ctx, reparsed, "bool", 4);
    TestAssertEquals(bool_val.kind, DRJSON_BOOL);
    TestAssertEquals(bool_val.boolean, 1);

    DrJsonValue arr_val = drjson_query(ctx, reparsed, "array", 5);
    TestAssertEquals(arr_val.kind, DRJSON_ARRAY);
    TestAssertEquals(drjson_len(ctx, arr_val), 3);

    DrJsonValue elem0 = drjson_get_by_index(ctx, arr_val, 0);
    TestAssert(elem0.kind == DRJSON_INTEGER || elem0.kind == DRJSON_UINTEGER);
    if(elem0.kind == DRJSON_INTEGER)
        TestAssertEquals(elem0.integer, 1);
    else
        TestAssertEquals(elem0.uinteger, 1);

    DrJsonValue elem1 = drjson_get_by_index(ctx, arr_val, 1);
    TestAssert(elem1.kind == DRJSON_INTEGER || elem1.kind == DRJSON_UINTEGER);
    if(elem1.kind == DRJSON_INTEGER)
        TestAssertEquals(elem1.integer, 2);
    else
        TestAssertEquals(elem1.uinteger, 2);

    DrJsonValue elem2 = drjson_get_by_index(ctx, arr_val, 2);
    TestAssert(elem2.kind == DRJSON_INTEGER || elem2.kind == DRJSON_UINTEGER);
    if(elem2.kind == DRJSON_INTEGER)
        TestAssertEquals(elem2.integer, 3);
    else
        TestAssertEquals(elem2.uinteger, 3);

    // Test drjson_print_error_fd and drjson_print_error_mem
    const char* invalid_json = "{ foo: }";
    DrJsonParseContext pctx = {
        .begin = invalid_json,
        .cursor = invalid_json,
        .end = invalid_json + strlen(invalid_json),
        .ctx = ctx,
        .depth = 0,
    };
    DrJsonValue error_val = drjson_parse(&pctx, 0);
    TestAssertEquals(error_val.kind, DRJSON_ERROR);

    size_t error_line, error_column;
    drjson_get_line_column(&pctx, &error_line, &error_column);

    // Test drjson_print_error_mem first (easier to verify)
    char mem_buffer[512];
    err = drjson_print_error_mem(mem_buffer, sizeof(mem_buffer), "test.json", 9, error_line, error_column, error_val);
    TestAssertFalse(err);
    size_t mem_len = strlen(mem_buffer);
    TestAssert(mem_len > 0);

    // Verify exact format: "test.json:line:column: Error: ...\n"
    // Format is: filename:line+1:column+1: <error>\n
    char expected_prefix[128];
    snprintf(expected_prefix, sizeof(expected_prefix), "test.json:%zu:%zu: Error: ", error_line + 1, error_column + 1);
    TestAssert(strncmp(mem_buffer, expected_prefix, strlen(expected_prefix)) == 0);
    TestAssert(mem_buffer[mem_len - 1] == '\n');

    // Test drjson_print_error_fd produces same output
    char temp_file2[] = "/tmp/drjson_error_XXXXXX";
    fd = mkstemp(temp_file2);
    TestAssert(fd >= 0);

    err = drjson_print_error_fd(fd, "test.json", 9, error_line, error_column, error_val);
    TestAssertFalse(err);

    // Seek back to beginning to read what we wrote
    seek_result = lseek(fd, 0, SEEK_SET);
    TestAssert(seek_result == 0);

    char fd_buffer[512];
    bytes_read = read(fd, fd_buffer, sizeof(fd_buffer) - 1);
    TestAssert(bytes_read > 0);
    fd_buffer[bytes_read] = '\0';

    close(fd);
    unlink(temp_file2);

    // Both outputs should be identical
    TestAssertEquals(bytes_read, (ssize_t)mem_len);
    TestAssert(memcmp(mem_buffer, fd_buffer, mem_len) == 0);

    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
#endif
    TESTEND();
}

TestFunction(TestFloatPrinting){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test printing various float values to exercise fpconv library
    // Each test case has exact expected output
    struct {
        double value;
        StringView expected;
    } test_cases[] = {
        {3.14159265358979, SV("3.14159265358979")},
        {1.0e10, SV("1e+10")},
        {1.0e-10, SV("1e-10")},
        {0.0, SV("0")},
        {-0.0, SV("-0")},
        {123.456, SV("123.456")},
        {0.000001, SV("0.000001")},
        {999999.999999, SV("999999.999999")},
        {1.7976931348623157e308, SV("1.7976931348623157e+308")},
        {2.2250738585072014e-308, SV("2.2250738585072014e-308")},
    };

    for(size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++){
        DrJsonValue num = drjson_make_number(test_cases[i].value);

        // Print just the number - this exercises fpconv_dtoa for float-to-string conversion
        char buffer[512];
        size_t printed;
        int err = drjson_print_value_mem(ctx, buffer, sizeof(buffer), num, 0,
                                         DRJSON_APPEND_ZERO, &printed);
        TestAssertFalse(err);
        TestAssert(printed > 0);
        TestAssert(buffer[printed-1] == '\0');

        // Verify exact format match
        StringView actual = {.length = printed - 1, .text = buffer};
        TestAssertEquals2(SV_equals, actual, test_cases[i].expected);

        // Round-trip: parse it back and verify we get the exact same value
        DrJsonValue reparsed = drjson_parse_string(ctx, buffer, printed - 1, 0);
        // Could be NUMBER, INTEGER, or UINTEGER depending on the value
        // (e.g., 1.0e10 prints as "10000000000" which parses as UINTEGER)
        TestAssert(reparsed.kind == DRJSON_NUMBER ||
                   reparsed.kind == DRJSON_INTEGER ||
                   reparsed.kind == DRJSON_UINTEGER);
        // Get the numeric value regardless of type
        double reparsed_value;
        if(reparsed.kind == DRJSON_NUMBER)
            reparsed_value = reparsed.number;
        else if(reparsed.kind == DRJSON_INTEGER)
            reparsed_value = (double)reparsed.integer;
        else
            reparsed_value = (double)reparsed.uinteger;

        // The reparsed value should exactly match the original
        TestAssert(reparsed_value == test_cases[i].value);

        // Clean up for next iteration
        drjson_gc(ctx, NULL, 0);
    }

    // Test an array with multiple floats
    DrJsonValue arr = drjson_make_array(ctx);
    int err = drjson_array_push_item(ctx, arr, drjson_make_number(1.1));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, arr, drjson_make_number(2.2));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, arr, drjson_make_number(3.3));
    TestAssertFalse(err);

    char buffer[1024];
    size_t printed;
    err = drjson_print_value_mem(ctx, buffer, sizeof(buffer), arr, 0,
                                 DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed > 0);

    // Verify exact format
    StringView actual = {.length = printed - 1, .text = buffer};
    TestAssertEquals2(SV_equals, actual, SV("[1.1,2.2,3.3]"));

    // Round-trip
    DrJsonValue reparsed_arr = drjson_parse_string(ctx, buffer, printed - 1, 0);
    TestAssertEquals(reparsed_arr.kind, DRJSON_ARRAY);
    TestAssertEquals(drjson_len(ctx, reparsed_arr), 3);

    DrJsonValue elem0 = drjson_get_by_index(ctx, reparsed_arr, 0);
    TestAssertEquals(elem0.kind, DRJSON_NUMBER);
    TestAssert(elem0.number == 1.1);

    DrJsonValue elem1 = drjson_get_by_index(ctx, reparsed_arr, 1);
    TestAssertEquals(elem1.kind, DRJSON_NUMBER);
    TestAssert(elem1.number == 2.2);

    DrJsonValue elem2 = drjson_get_by_index(ctx, reparsed_arr, 2);
    TestAssertEquals(elem2.kind, DRJSON_NUMBER);
    TestAssert(elem2.number == 3.3);

    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestCheckedQuery){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Create test structure: {"name": "Alice", "age": 30, "scores": [95, 87, 92]}
    DrJsonValue obj = drjson_make_object(ctx);
    int err = drjson_object_set_item_no_copy_key(ctx, obj, "name", 4,
                                                 drjson_make_string(ctx, "Alice", 5));
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "age", 3, drjson_make_int(30));
    TestAssertFalse(err);

    DrJsonValue scores = drjson_make_array(ctx);
    err = drjson_array_push_item(ctx, scores, drjson_make_int(95));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, scores, drjson_make_int(87));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, scores, drjson_make_int(92));
    TestAssertFalse(err);

    err = drjson_object_set_item_no_copy_key(ctx, obj, "scores", 6, scores);
    TestAssertFalse(err);

    // Test checked_query with correct type
    DrJsonValue result = drjson_checked_query(ctx, obj, DRJSON_STRING, "name", 4);
    TestAssertEquals(result.kind, DRJSON_STRING);
    const char* str;
    size_t len;
    err = drjson_get_str_and_len(ctx, result, &str, &len);
    TestAssertFalse(err);
    TestAssertEquals(len, 5);
    TestAssert(memcmp(str, "Alice", 5) == 0);

    // Test checked_query with integer
    result = drjson_checked_query(ctx, obj, DRJSON_INTEGER, "age", 3);
    // Note: could be UINTEGER instead depending on parsing
    TestAssert(result.kind == DRJSON_INTEGER || result.kind == DRJSON_UINTEGER);
    if(result.kind == DRJSON_INTEGER)
        TestAssertEquals(result.integer, 30);
    else
        TestAssertEquals(result.uinteger, 30);

    // Test checked_query with array
    result = drjson_checked_query(ctx, obj, DRJSON_ARRAY, "scores", 6);
    TestAssertEquals(result.kind, DRJSON_ARRAY);
    TestAssertEquals(drjson_len(ctx, result), 3);

    // Test checked_query with wrong type - should return error
    result = drjson_checked_query(ctx, obj, DRJSON_OBJECT, "name", 4);
    TestAssertEquals(result.kind, DRJSON_ERROR);

    result = drjson_checked_query(ctx, obj, DRJSON_ARRAY, "age", 3);
    TestAssertEquals(result.kind, DRJSON_ERROR);

    // Test checked_query with non-existent key - should return error
    result = drjson_checked_query(ctx, obj, DRJSON_STRING, "nonexistent", 11);
    TestAssertEquals(result.kind, DRJSON_ERROR);

    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestClearValue){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Test clearing a simple value (should be no-op)
    DrJsonValue num = drjson_make_int(42);
    drjson_clear(ctx, num);

    // Test clearing an array
    DrJsonValue arr = drjson_make_array(ctx);
    int err = drjson_array_push_item(ctx, arr, drjson_make_int(1));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, arr, drjson_make_int(2));
    TestAssertFalse(err);
    err = drjson_array_push_item(ctx, arr, drjson_make_int(3));
    TestAssertFalse(err);

    TestAssertEquals(drjson_len(ctx, arr), 3);
    drjson_clear(ctx, arr);
    TestAssertEquals(drjson_len(ctx, arr), 0);

    // Test clearing an object
    DrJsonValue obj = drjson_make_object(ctx);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "a", 1, drjson_make_int(1));
    TestAssertFalse(err);
    err = drjson_object_set_item_no_copy_key(ctx, obj, "b", 1, drjson_make_int(2));
    TestAssertFalse(err);

    TestAssertEquals(drjson_len(ctx, obj), 2);
    int clear_result = drjson_clear(ctx, obj);
    TestAssertFalse(clear_result);
    // BUG: drjson_clear for objects doesn't reset count (line 1286 in drjson.c)
    // It only zeros the hash table but forgets to set object->count = 0
    TestAssertEquals(drjson_len(ctx, obj), 0);  // This will fail until bug is fixed

    // Verify we can still use the cleared containers
    err = drjson_array_push_item(ctx, arr, drjson_make_int(99));
    TestAssertFalse(err);
    TestAssertEquals(drjson_len(ctx, arr), 1);

    // After clear, object should be usable again
    err = drjson_object_set_item_no_copy_key(ctx, obj, "new", 3, drjson_make_int(99));
    TestAssertFalse(err);
    // This should be 1, but will show the actual count after the buggy clear
    TestAssertEquals(drjson_len(ctx, obj), 1);

    drjson_gc(ctx, 0, 0);
    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestArraySwap){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Create an array [1, 2, 3, 4, 5]
    DrJsonValue arr = drjson_make_array(ctx);
    for(int i = 1; i <= 5; i++){
        drjson_array_push_item(ctx, arr, drjson_make_int(i));
    }

    // Swap indices 1 and 3 (values 2 and 4)
    int err = drjson_array_swap_items(ctx, arr, 1, 3);
    TestAssertEquals(err, 0);

    // Verify the swap
    DrJsonValue v1 = drjson_get_by_index(ctx, arr, 1);
    TestAssertEquals(v1.kind, DRJSON_INTEGER);
    TestAssertEquals(v1.integer, 4);

    DrJsonValue v3 = drjson_get_by_index(ctx, arr, 3);
    TestAssertEquals(v3.kind, DRJSON_INTEGER);
    TestAssertEquals(v3.integer, 2);

    // Test swapping same index (should be no-op)
    err = drjson_array_swap_items(ctx, arr, 2, 2);
    TestAssertEquals(err, 0);

    // Test swapping first and last
    err = drjson_array_swap_items(ctx, arr, 0, 4);
    TestAssertEquals(err, 0);

    DrJsonValue first = drjson_get_by_index(ctx, arr, 0);
    TestAssertEquals(first.integer, 5);

    DrJsonValue last = drjson_get_by_index(ctx, arr, 4);
    TestAssertEquals(last.integer, 1);

    // Test error cases
    err = drjson_array_swap_items(ctx, arr, 0, 99);  // Out of bounds
    TestAssertEquals(err, 1);

    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestArrayMove){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Create an array ["a", "b", "c", "d", "e"]
    DrJsonValue arr = drjson_make_array(ctx);
    const char* letters[] = {"a", "b", "c", "d", "e"};
    for(int i = 0; i < 5; i++){
        drjson_array_push_item(ctx, arr, drjson_make_string(ctx, letters[i], 1));
    }

    // Move "c" (index 2) to index 4 (end)
    int err = drjson_array_move_item(ctx, arr, 2, 4);
    TestAssertEquals(err, 0);

    // Verify: ["a", "b", "d", "e", "c"]
    StringView v0, v1, v2, v3, v4;
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 0), &v0.text, &v0.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 1), &v1.text, &v1.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 2), &v2.text, &v2.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 3), &v3.text, &v3.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 4), &v4.text, &v4.length);
    TestAssertEquals(err, 0);

    TestAssertEquals2(SV_equals, v0, SV("a"));
    TestAssertEquals2(SV_equals, v1, SV("b"));
    TestAssertEquals2(SV_equals, v2, SV("d"));
    TestAssertEquals2(SV_equals, v3, SV("e"));
    TestAssertEquals2(SV_equals, v4, SV("c"));

    // Move "a" (index 0) to index 2
    err = drjson_array_move_item(ctx, arr, 0, 2);
    TestAssertEquals(err, 0);

    // Verify: ["b", "d", "a", "e", "c"]
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 0), &v0.text, &v0.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 1), &v1.text, &v1.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, arr, 2), &v2.text, &v2.length);
    TestAssertEquals(err, 0);

    TestAssertEquals2(SV_equals, v0, SV("b"));
    TestAssertEquals2(SV_equals, v1, SV("d"));
    TestAssertEquals2(SV_equals, v2, SV("a"));

    // Test moving to same index (no-op)
    err = drjson_array_move_item(ctx, arr, 1, 1);
    TestAssertEquals(err, 0);

    drjson_ctx_free_all(ctx);
    assert_all_freed();
    TESTEND();
}

TestFunction(TestObjectMove){
    TESTBEGIN();
    DrJsonContext* ctx = drjson_create_ctx(get_test_allocator());

    // Create object {"a": 1, "b": 2, "c": 3, "d": 4}
    DrJsonValue obj = drjson_make_object(ctx);
    drjson_object_set_item_no_copy_key(ctx, obj, "a", 1, drjson_make_int(1));
    drjson_object_set_item_no_copy_key(ctx, obj, "b", 1, drjson_make_int(2));
    drjson_object_set_item_no_copy_key(ctx, obj, "c", 1, drjson_make_int(3));
    drjson_object_set_item_no_copy_key(ctx, obj, "d", 1, drjson_make_int(4));

    // Move item at index 1 ("b": 2) to index 3 (end)
    int err = drjson_object_move_item(ctx, obj, 1, 3);
    TestAssertEquals(err, 0);

    // Verify new order: {"a": 1, "c": 3, "d": 4, "b": 2}
    DrJsonValue keys = drjson_object_keys(obj);
    StringView k0, k1, k2, k3;
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, keys, 0), &k0.text, &k0.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, keys, 1), &k1.text, &k1.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, keys, 2), &k2.text, &k2.length);
    TestAssertEquals(err, 0);
    err = drjson_get_str_and_len(ctx, drjson_get_by_index(ctx, keys, 3), &k3.text, &k3.length);
    TestAssertEquals(err, 0);

    TestAssertEquals2(SV_equals, k0, SV("a"));
    TestAssertEquals2(SV_equals, k1, SV("c"));
    TestAssertEquals2(SV_equals, k2, SV("d"));
    TestAssertEquals2(SV_equals, k3, SV("b"));

    // Verify values still match keys
    DrJsonValue values = drjson_object_values(obj);
    DrJsonValue v0 = drjson_get_by_index(ctx, values, 0);
    DrJsonValue v1 = drjson_get_by_index(ctx, values, 1);
    DrJsonValue v2 = drjson_get_by_index(ctx, values, 2);
    DrJsonValue v3 = drjson_get_by_index(ctx, values, 3);

    TestAssertEquals(v0.integer, 1);
    TestAssertEquals(v1.integer, 3);
    TestAssertEquals(v2.integer, 4);
    TestAssertEquals(v3.integer, 2);

    // Verify lookups still work after moving
    DrJsonValue val_b = drjson_query(ctx, obj, "b", 1);
    TestAssertEquals(val_b.kind, DRJSON_INTEGER);
    TestAssertEquals(val_b.integer, 2);

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

#include "test_allocator.c"
