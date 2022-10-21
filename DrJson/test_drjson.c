#ifdef _WIN32
#ifndef _CRT_NONSTDC_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif
#include "drjson.h"
#include "testing.h"
static TestFunc TestSimpleParsing;
static TestFunc TestDoubleParsing;
static TestFunc TestSerialization;

int main(int argc, char** argv){
    RegisterTest(TestSimpleParsing);
    RegisterTest(TestDoubleParsing);
    RegisterTest(TestSerialization);
    return test_main(argc, argv, NULL);
}

TestFunction(TestSimpleParsing){
    TESTBEGIN();
    const char* example = "{ hello world }";
    DrJsonContext ctx = {
        .allocator = drjson_stdc_allocator(),
    };
    DrJsonParseContext pctx = {
        .begin = example,
        .cursor = example,
        .end = example + strlen(example),
        .ctx = &ctx,
    };
    DrJsonValue v = drjson_parse(&pctx);
    TestAssertNotEqual((int)drjson_kind(v), DRJSON_ERROR);
    TestAssertEquals((int)drjson_kind(v), DRJSON_OBJECT);

    DrJsonValue q = drjson_query(&ctx, v, "hello", strlen("hello"));
    TestAssertNotEqual((int)drjson_kind(q), DRJSON_ERROR);
    TestAssertEquals((int)drjson_kind(q), DRJSON_STRING);
    TestAssertEquals(drjson_len(&ctx, q), sizeof("world")-1);
    TestAssert(memcmp(q.string, "world", sizeof("world")-1)==0);

    DrJsonValue val = drjson_object_get_item(&ctx, v, "hello", strlen("hello"), 0);
    TestAssertNotEqual((int)drjson_kind(val), DRJSON_ERROR);
    TestAssert(drjson_eq(q, val));

    drjson_ctx_free_all(&ctx);
    TESTEND();
}

TestFunction(TestDoubleParsing){
    TESTBEGIN();
    double NaN = NAN;
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
        DrJsonContext ctx = {
            .allocator = drjson_stdc_allocator(),
        };
        DrJsonValue v = drjson_parse_string(&ctx, example, strlen(example), 0);
        TestAssertNotEqual((int)drjson_kind(v), DRJSON_ERROR);
        TestAssertEquals((int)drjson_kind(v), DRJSON_NUMBER);
        TestAssertEquals(v.number, cases[i].value);
        drjson_ctx_free_all(&ctx);
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
    DrJsonContext ctx = {
        .allocator = drjson_stdc_allocator(),
    };
    DrJsonValue v = drjson_parse_string(&ctx, example, strlen(example), 0);
    TestAssertNotEqual((int)drjson_kind(v), DRJSON_ERROR);
    char buff[512];
    size_t printed;
    int err = drjson_print_value_mem(&ctx, buff, sizeof buff, v, 0, DRJSON_APPEND_ZERO, &printed);
    TestAssertFalse(err);
    TestAssert(printed <= sizeof buff);
    TestAssert(printed);
    TestAssertEquals(buff[printed-1], '\0');
    TestAssertEquals2(str_eq, buff, "{\"foo\":{\"bar\":{\"bazinga\":3}}}");
    drjson_ctx_free_all(&ctx);
    TESTEND();
}
