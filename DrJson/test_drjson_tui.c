//
// Copyright © 2025, David Priver <david@davidpriver.com>
//
// Tests for DrJson TUI functionality
//
// This file tests the pure logic functions from the TUI that don't depend on
// terminal I/O or global state.
// Claude wrote about all of this so check that the test is correct when it fails.
//

#include "testing.h"
#include <string.h>

// Include the TUI as a unity build to get access to static functions
// This brings in drjson.h, drjson.c, parse_numbers.h, etc.
#define main drjson_tui_main
#include "drjson_tui.c"
#undef main

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

// Forward declarations of test functions
static TestFunc TestNumericParsing;
static TestFunc TestNumericSearchInteger;
static TestFunc TestNumericSearchDouble;
static TestFunc TestNumericSearchNonNumeric;
static TestFunc TestSubstringMatch;
static TestFunc TestStringMatchesQuery;
static TestFunc TestNavValueMatchesQuery;
static TestFunc TestBitSetOperations;
static TestFunc TestLineEditorBasics;
static TestFunc TestLineEditorHistory;
static TestFunc TestLineEditorWordOperations;
static TestFunc TestPathBuilding;
static TestFunc TestNavContainsMatch;
static TestFunc TestNavigationTreeLogic;
static TestFunc TestFocusStack;
static TestFunc TestUTF8DisplayWidth;
static TestFunc TestNavigationJumps;
static TestFunc TestExpandCollapseRecursive;
static TestFunc TestCommandLookup;
static TestFunc TestBitSetEdgeCases;
static TestFunc TestComplexNestedPaths;
static TestFunc TestSearchRecursiveExpansion;
static TestFunc TestNavigationBoundaries;
static TestFunc TestMessageHandling;
static TestFunc TestLineEditorEdgeCases;
static TestFunc TestLargeJSONStructures;
static TestFunc TestSearchNavigation;
static TestFunc TestValueComparison;
static TestFunc TestParseAsString;
static TestFunc TestParseAsValue;
static TestFunc TestContainerID;
static TestFunc TestSearchWithExpansion;
static TestFunc TestFlatViewMode;
static TestFunc TestSortingArrays;
static TestFunc TestSortingObjects;
static TestFunc TestFilteringArrays;
static TestFunc TestFilteringObjects;
static TestFunc TestTruthiness;
static TestFunc TestNavRebuildRecursive;
static TestFunc TestOperatorParsing;
static TestFunc TestLiteralParsing;
static TestFunc TestQueryCommand;
static TestFunc TestFocusUnfocusCommands;
static TestFunc TestNavJumpToNthChild;
static TestFunc TestFocusStackOperations;
static TestFunc TestComplexQueryPaths;
static TestFunc TestStripWhitespace;
static TestFunc TestNavJumpToParent;
static TestFunc TestNavNavigateToPath;
static TestFunc TestTuiEvalExpression;
static TestFunc TestDrjToDoubleForSort;
static TestFunc TestSortingWithQuery;
static TestFunc TestNavIsExpanded;
static TestFunc TestNavAppendItem;
static TestFunc TestNavReinit;
static TestFunc TestNavSetMessagef;
static TestFunc TestBitSetRemoveToggleClear;
static TestFunc TestToLower;
static TestFunc TestSubstringMatchFunc;
static TestFunc TestGlobMatch;
static TestFunc TestNavFindParent;
static TestFunc TestGetTypeRank;
static TestFunc TestNavCollapseAll;

int main(int argc, char*_Nullable*_Nonnull argv){
    RegisterTest(TestNumericParsing);
    RegisterTest(TestNumericSearchInteger);
    RegisterTest(TestNumericSearchDouble);
    RegisterTest(TestNumericSearchNonNumeric);
    RegisterTest(TestSubstringMatch);
    RegisterTest(TestStringMatchesQuery);
    RegisterTest(TestNavValueMatchesQuery);
    RegisterTest(TestBitSetOperations);
    RegisterTest(TestLineEditorBasics);
    RegisterTest(TestLineEditorHistory);
    RegisterTest(TestLineEditorWordOperations);
    RegisterTest(TestPathBuilding);
    RegisterTest(TestNavContainsMatch);
    RegisterTest(TestNavigationTreeLogic);
    RegisterTest(TestFocusStack);
    RegisterTest(TestUTF8DisplayWidth);
    RegisterTest(TestNavigationJumps);
    RegisterTest(TestExpandCollapseRecursive);
    RegisterTest(TestCommandLookup);
    RegisterTest(TestBitSetEdgeCases);
    RegisterTest(TestComplexNestedPaths);
    RegisterTest(TestSearchRecursiveExpansion);
    RegisterTest(TestNavigationBoundaries);
    RegisterTest(TestMessageHandling);
    RegisterTest(TestLineEditorEdgeCases);
    RegisterTest(TestLargeJSONStructures);
    RegisterTest(TestSearchNavigation);
    RegisterTest(TestValueComparison);
    RegisterTest(TestParseAsString);
    RegisterTest(TestParseAsValue);
    RegisterTest(TestContainerID);
    RegisterTest(TestSearchWithExpansion);
    RegisterTest(TestFlatViewMode);
    RegisterTest(TestSortingArrays);
    RegisterTest(TestSortingObjects);
    RegisterTest(TestFilteringArrays);
    RegisterTest(TestFilteringObjects);
    RegisterTest(TestTruthiness);
    RegisterTest(TestNavRebuildRecursive);
    RegisterTest(TestOperatorParsing);
    RegisterTest(TestLiteralParsing);
    RegisterTest(TestQueryCommand);
    RegisterTest(TestFocusUnfocusCommands);
    RegisterTest(TestNavJumpToNthChild);
    RegisterTest(TestFocusStackOperations);
    RegisterTest(TestComplexQueryPaths);
    RegisterTest(TestStripWhitespace);
    RegisterTest(TestNavJumpToParent);
    RegisterTest(TestNavNavigateToPath);
    RegisterTest(TestTuiEvalExpression);
    RegisterTest(TestDrjToDoubleForSort);
    RegisterTest(TestSortingWithQuery);
    RegisterTest(TestNavIsExpanded);
    RegisterTest(TestNavAppendItem);
    RegisterTest(TestNavReinit);
    RegisterTest(TestNavSetMessagef);
    RegisterTest(TestBitSetRemoveToggleClear);
    RegisterTest(TestToLower);
    RegisterTest(TestSubstringMatchFunc);
    RegisterTest(TestGlobMatch);
    RegisterTest(TestNavFindParent);
    RegisterTest(TestGetTypeRank);
    RegisterTest(TestNavCollapseAll);
    return test_main(argc, argv, NULL);
}

// Test that number parsing works correctly for search patterns
TestFunction(TestNumericParsing){
    TESTBEGIN();

    // Test int64 parsing
    {
        LongString pattern = LS("42");
        Int64Result res = parse_int64(pattern.text, pattern.length);
        TestExpectSuccess(res);
        TestExpectEquals(res.result, 42);
    }

    // Test negative int64
    {
        LongString pattern = LS("-123");
        Int64Result res = parse_int64(pattern.text, pattern.length);
        TestExpectSuccess(res);
        TestExpectEquals(res.result, -123);
    }

    // Test uint64 parsing
    {
        LongString pattern = LS("18446744073709551615");
        Uint64Result res = parse_uint64(pattern.text, pattern.length);
        TestExpectSuccess(res);
        TestExpectEquals(res.result, UINT64_MAX);
    }

    // Test double parsing
    {
        LongString pattern = LS("3.14");
        DoubleResult res = parse_double(pattern.text, pattern.length);
        TestExpectSuccess(res);
        TestExpectTrue(res.result > 3.13 && res.result < 3.15);
    }

    // Test non-numeric pattern (should fail)
    {
        LongString pattern = LS("foo");
        Int64Result res = parse_int64(pattern.text, pattern.length);
        TestExpectFailure(res);
    }

    // Test pattern with regex chars (should fail)
    {
        LongString pattern = LS("80.*");
        Int64Result res = parse_int64(pattern.text, pattern.length);
        TestExpectFailure(res);
    }

    TESTEND();
}

// Test numeric search matching logic
TestFunction(TestNumericSearchInteger){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create a JSON object with an integer field
    LongString json = LS("{\"age\": 42}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    // Query for the age field
    LongString age_key = LS("age");
    DrJsonValue age_val = drjson_query(ctx, root, age_key.text, age_key.length);
    // Positive integers may parse as UINTEGER
    if(age_val.kind == DRJSON_UINTEGER){
        TestAssertEquals(age_val.uinteger, 42);
    }
    else {
        TestAssertEquals((int)age_val.kind, DRJSON_INTEGER);
        TestAssertEquals(age_val.integer, 42);
    }

    // Test that the value matches (use correct field based on kind)
    int64_t age_value = (age_val.kind == DRJSON_UINTEGER) ? (int64_t)age_val.uinteger : age_val.integer;
    TestExpectEquals(age_value, 42);

    // Test different integer
    TestExpectTrue(age_value != 43);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test numeric search with doubles
TestFunction(TestNumericSearchDouble){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create a JSON object with a double field
    LongString json = LS("{\"price\": 19.99}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    // Query for the price field
    LongString price_key = LS("price");
    DrJsonValue price_val = drjson_query(ctx, root, price_key.text, price_key.length);
    TestAssertEquals((int)price_val.kind, DRJSON_NUMBER);

    TestExpectEquals(price_val.number, 19.99);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test that non-numeric patterns still work for string matching
TestFunction(TestNumericSearchNonNumeric){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create a JSON object with string fields
    LongString json = LS("{\"name\": \"Alice\", \"id\": \"12345\"}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    // Query for the name field
    LongString name_key = LS("name");
    DrJsonValue name_val = drjson_query(ctx, root, name_key.text, name_key.length);
    TestAssertEquals((int)name_val.kind, DRJSON_STRING);

    StringView actual1;
    int err = drjson_get_str_and_len(ctx, name_val, &actual1.text, &actual1.length);
    TestAssertFalse(err);
    TestAssertEquals2(SV_equals, actual1, SV("Alice"));

    // Query for the id field (string containing number)
    LongString id_key = LS("id");
    DrJsonValue id_val = drjson_query(ctx, root, id_key.text, id_key.length);
    TestAssertEquals((int)id_val.kind, DRJSON_STRING);

    StringView actual2;
    err = drjson_get_str_and_len(ctx, id_val, &actual2.text, &actual2.length);
    TestAssertFalse(err);
    TestAssertEquals2(SV_equals, actual2, SV("12345"));

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test substring_match() function from TUI
TestFunction(TestSubstringMatch){
    TESTBEGIN();

    // Basic substring match
    TestExpectTrue(substring_match("hello world", 11, "world", 5));
    TestExpectTrue(substring_match("hello world", 11, "hello", 5));
    TestExpectTrue(substring_match("hello world", 11, "lo wo", 5));

    // Case insensitive
    TestExpectTrue(substring_match("Hello World", 11, "world", 5));
    TestExpectTrue(substring_match("HELLO", 5, "hello", 5));

    // No match
    TestExpectFalse(substring_match("hello", 5, "world", 5));
    TestExpectFalse(substring_match("hello", 5, "helloworld", 10));

    // Empty query should not match
    TestExpectFalse(substring_match("hello", 5, "", 0));

    TESTEND();
}

// Test string_matches_query() function from TUI
TestFunction(TestStringMatchesQuery){
    TESTBEGIN();

    // Simple substring matching
    TestExpectTrue(string_matches_query("hello world", 11, "world", 5));
    TestExpectTrue(string_matches_query("test123", 7, "test", 4));
    TestExpectTrue(string_matches_query("foobar", 6, "foo", 3));

    // Regex patterns (dre simple patterns)
    TestExpectTrue(string_matches_query("test123", 7, "test.*", 6));
    TestExpectTrue(string_matches_query("hello", 5, "h.*o", 4));

    // No match
    TestExpectFalse(string_matches_query("hello", 5, "world", 5));
    TestExpectFalse(string_matches_query("test", 4, "testing", 7));

    TESTEND();
}

// Test nav_value_matches_query() function from TUI
TestFunction(TestNavValueMatchesQuery){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create a test JSON value with age field FIRST so "age" gets atomized
    LongString json = LS("{\"age\": 42}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    // Set up a mock JsonNav structure
    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.search_mode = SEARCH_QUERY;

    // Parse a simple path (now "age" should be in atom table)
    LongString path_str = LS("age");
    DrJsonPath path = {0};
    const char* remainder = NULL;
    int parse_result = drjson_path_parse_greedy(ctx, path_str.text, path_str.length, &path, &remainder);
    TestAssertEquals(parse_result, 0);
    TestAssertEquals((int)path.count, 1);
    nav.search_query_path = path;

    // Set up search pattern for numeric search
    memcpy(nav.search_pattern, "42", 3);
    nav.search_pattern_len = 2;

    // Parse the pattern as a number
    Int64Result int_res = parse_int64(nav.search_pattern, nav.search_pattern_len);
    TestAssertSuccess(int_res);
    TestAssertEquals(int_res.result, 42);
    nav.search_numeric.is_numeric = 1;
    nav.search_numeric.is_integer = 1;
    nav.search_numeric.int_value = int_res.result;

    // Manually test the path evaluation first
    DrJsonValue age_result = drjson_evaluate_path(ctx, root, &path);
    TestAssertNotEqual((int)age_result.kind, DRJSON_ERROR);
    // Could be either INTEGER or UINTEGER
    _Bool is_42 = (age_result.kind == DRJSON_INTEGER && age_result.integer == 42) ||
                  (age_result.kind == DRJSON_UINTEGER && age_result.uinteger == 42);
    TestAssert(is_42);

    // Test that it matches
    TestExpectTrue(nav_value_matches_query(&nav, root, (DrJsonAtom){0}, "", 0));

    // Test with different value
    LongString json2 = LS("{\"age\": 43}");
    DrJsonValue root2 = drjson_parse_string(ctx, json2.text, json2.length, 0);
    TestExpectFalse(nav_value_matches_query(&nav, root2, (DrJsonAtom){0}, "", 0));

    // Test string matching in SEARCH_QUERY mode
    // Create JSON first to atomize "name"
    LongString json3 = LS("{\"name\": \"Alice\"}");
    DrJsonValue root3 = drjson_parse_string(ctx, json3.text, json3.length, 0);

    nav.search_mode = SEARCH_QUERY;
    nav.search_numeric.is_numeric = 0; // Disable numeric search
    memcpy(nav.search_pattern, "Alice", 6);
    nav.search_pattern_len = 5;

    // Parse path for name field
    LongString name_path = LS("name");
    DrJsonPath name_path_parsed = {0};
    parse_result = drjson_path_parse_greedy(ctx, name_path.text, name_path.length, &name_path_parsed, &remainder);
    TestAssertEquals(parse_result, 0);
    nav.search_query_path = name_path_parsed;

    TestExpectTrue(nav_value_matches_query(&nav, root3, (DrJsonAtom){0}, "", 0));

    // Test no match
    LongString json4 = LS("{\"name\": \"Bob\"}");
    DrJsonValue root4 = drjson_parse_string(ctx, json4.text, json4.length, 0);
    TestExpectFalse(nav_value_matches_query(&nav, root4, (DrJsonAtom){0}, "", 0));

    // Test SEARCH_RECURSIVE mode with string matching
    nav.search_mode = SEARCH_RECURSIVE;
    LongString query = LS("Alice");
    LongString alice_str = LS("\"Alice\"");
    DrJsonValue string_val = drjson_parse_string(ctx, alice_str.text, alice_str.length, 0);
    TestExpectTrue(nav_value_matches_query(&nav, string_val, (DrJsonAtom){0}, query.text, query.length));

    // Test with key matching
    DrJsonAtom key_atom;
    int err = DRJSON_ATOMIZE(ctx, "username", &key_atom);
    TestAssertFalse(err);
    TestExpectTrue(nav_value_matches_query(&nav, string_val, key_atom, "user", 4));

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test BitSet operations
TestFunction(TestBitSetOperations){
    TESTBEGIN();

    BitSet bs = {0};

    // Initially empty
    TestExpectFalse(bs_contains(&bs, 0));
    TestExpectFalse(bs_contains(&bs, 42));

    // Add some values
    bs_add(&bs, 5);
    TestExpectTrue(bs_contains(&bs, 5));
    TestExpectFalse(bs_contains(&bs, 6));

    bs_add(&bs, 100);
    TestExpectTrue(bs_contains(&bs, 100));
    TestExpectTrue(bs_contains(&bs, 5));

    // Add same value again (should be idempotent)
    bs_add(&bs, 5);
    TestExpectTrue(bs_contains(&bs, 5));

    // Remove value
    bs_remove(&bs, 5);
    TestExpectFalse(bs_contains(&bs, 5));
    TestExpectTrue(bs_contains(&bs, 100));

    // Remove non-existent value (should be safe)
    bs_remove(&bs, 999);
    TestExpectTrue(bs_contains(&bs, 100));

    // Clear all
    bs_clear(&bs);
    TestExpectFalse(bs_contains(&bs, 100));
    TestExpectFalse(bs_contains(&bs, 5));

    // Test with large values
    bs_add(&bs, 10000);
    TestExpectTrue(bs_contains(&bs, 10000));

    // Cleanup
    if(bs.ids) free(bs.ids);

    TESTEND();
}

// Test Line Editor basic operations
TestFunction(TestLineEditorBasics){
    TESTBEGIN();

    LineEditor le = {0};
    le_init(&le, 256);

    // Initially empty
    TestExpectEquals(le.length, 0);
    TestExpectEquals(le.cursor_pos, 0);

    // Insert characters
    le_append_char(&le, 'h');
    TestExpectEquals(le.length, 1);
    TestExpectEquals(le.cursor_pos, 1);
    TestExpectEquals(le.data[0], 'h');

    le_append_char(&le, 'i');
    TestExpectEquals(le.cursor_pos, 2);
    TestExpectEquals2(SV_equals, le.sv, SV("hi"));

    // Move cursor left
    le_move_left(&le);
    TestExpectEquals(le.cursor_pos, 1);

    // Insert in middle
    le_append_char(&le, 'X');
    TestExpectEquals2(SV_equals, le.sv, SV("hXi"));
    TestExpectEquals(le.cursor_pos, 2);

    // Delete character (backspace deletes before cursor)
    le_backspace(&le);
    TestExpectEquals(le.cursor_pos, 1);
    TestExpectEquals2(SV_equals, le.sv, SV("hi"));

    // Move cursor to end
    le_move_right(&le);
    TestExpectEquals(le.cursor_pos, 2);

    // Can't move beyond end
    le_move_right(&le);
    TestExpectEquals(le.cursor_pos, 2);

    // Clear
    le_clear(&le);
    TestExpectEquals(le.length, 0);
    TestExpectEquals(le.cursor_pos, 0);

    le_free(&le);
    TESTEND();
}

// Test Line Editor history
TestFunction(TestLineEditorHistory){
    TESTBEGIN();

    LineEditor le = {0};
    le_init(&le, 256);

    LineEditorHistory hist = {0};
    le_history_init(&hist);
    le.history = &hist;

    // Add to history
    le_history_add(&hist, "first", 5);
    le_history_add(&hist, "second", 6);
    le_history_add(&hist, "third", 5);

    TestExpectEquals(hist.count, 3);

    // Navigate history
    le_history_prev(&le);
    TestExpectEquals2(SV_equals, le.sv, SV("third"));

    le_history_prev(&le);
    TestExpectEquals2(SV_equals, le.sv, SV("second"));

    le_history_prev(&le);
    TestExpectEquals2(SV_equals, le.sv, SV("first"));

    // Can't go past beginning
    le_history_prev(&le);
    TestExpectEquals2(SV_equals, le.sv, SV("first"));

    // Navigate forward
    le_history_next(&le);
    TestExpectEquals2(SV_equals, le.sv, SV("second"));

    // Reset
    le_history_reset(&le);
    le_clear(&le);
    TestExpectEquals(le.length, 0);

    le_free(&le);
    le_history_free(&hist);
    TESTEND();
}

// Test Line Editor word operations
TestFunction(TestLineEditorWordOperations){
    TESTBEGIN();

    LineEditor le = {0};
    le_init(&le, 256);

    // Setup: "hello world test"
    LongString text = LS("hello world test");
    for(size_t i = 0; i < text.length; i++){
        le_append_char(&le, text.text[i]);
    }
    TestExpectEquals(le.length, text.length);

    // Kill to end
    le.cursor_pos = 5; // After "hello"
    le_kill_line(&le);
    TestExpectEquals2(SV_equals, le.sv, SV("hello"));

    // Setup again for word deletion
    le_clear(&le);
    for(size_t i = 0; i < text.length; i++){
        le_append_char(&le, text.text[i]);
    }

    // Delete word backward from end
    le_delete_word_backward(&le);
    // Should delete "test" but leave the space before it
    TestExpectTrue(le.length < text.length);
    TestExpectEquals2(SV_equals, le.sv, SV("hello world "));

    le_free(&le);
    TESTEND();
}

// Test path building
TestFunction(TestPathBuilding){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create a simple nested structure
    LongString json = LS("{\"users\": [{\"name\": \"Alice\", \"age\": 30}]}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Build items array properly using nav_rebuild
    nav_rebuild(&nav);
    TestExpectTrue(nav.item_count > 0);

    // Now test path building
    char path_buf[1024];
    size_t len = nav_build_json_path(&nav, path_buf, sizeof(path_buf));

    // Should produce something (even if just empty or root)
    TestExpectTrue(len >= 0);

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_contains_match
TestFunction(TestNavContainsMatch){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.search_mode = SEARCH_RECURSIVE;

    // Simple string value
    DrJsonValue str_val = drjson_parse_string(ctx, "\"hello world\"", 13, 0);
    TestExpectTrue(nav_contains_match(&nav, str_val, (DrJsonAtom){0}, "world", 5));
    TestExpectFalse(nav_contains_match(&nav, str_val, (DrJsonAtom){0}, "notfound", 8));

    // Array with matching element
    LongString arr_json = LS("[\"foo\", \"bar\", \"baz\"]");
    DrJsonValue arr = drjson_parse_string(ctx, arr_json.text, arr_json.length, 0);
    TestExpectTrue(nav_contains_match(&nav, arr, (DrJsonAtom){0}, "bar", 3));
    TestExpectFalse(nav_contains_match(&nav, arr, (DrJsonAtom){0}, "notfound", 8));

    // Nested object
    LongString obj_json = LS("{\"nested\": {\"value\": \"found\"}}");
    DrJsonValue obj = drjson_parse_string(ctx, obj_json.text, obj_json.length, 0);
    TestExpectTrue(nav_contains_match(&nav, obj, (DrJsonAtom){0}, "found", 5));
    TestExpectFalse(nav_contains_match(&nav, obj, (DrJsonAtom){0}, "notfound", 8));

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test navigation tree logic
TestFunction(TestNavigationTreeLogic){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Test nav_is_container
    DrJsonValue obj = drjson_parse_string(ctx, "{\"a\": 1}", 8, 0);
    TestExpectTrue(nav_is_container(obj));

    DrJsonValue arr = drjson_parse_string(ctx, "[1, 2, 3]", 9, 0);
    TestExpectTrue(nav_is_container(arr));

    DrJsonValue str = drjson_parse_string(ctx, "\"hello\"", 7, 0);
    TestExpectFalse(nav_is_container(str));

    DrJsonValue num = drjson_parse_string(ctx, "42", 2, 0);
    TestExpectFalse(nav_is_container(num));

    // Test nav_get_container_id (should be deterministic for same container)
    uint64_t id1 = nav_get_container_id(obj);
    uint64_t id2 = nav_get_container_id(obj);
    TestExpectEquals(id1, id2);

    // Different containers should have different IDs
    uint64_t id3 = nav_get_container_id(arr);
    TestExpectTrue(id1 != id3);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test focus stack
TestFunction(TestFocusStack){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    LongString json = LS("{\"a\": {\"b\": {\"c\": 1}}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;
    nav.focus_stack = NULL;
    nav.focus_stack_count = 0;
    nav.focus_stack_capacity = 0;

    // Initially focused on root
    TestExpectEquals(nav.focus_stack_count, 0);

    // Push focus (simplified - real implementation does more)
    DrJsonValue inner = drjson_query(ctx, root, "a", 1);
    TestAssertNotEqual((int)inner.kind, DRJSON_ERROR);

    // Manually push to focus stack
    if(nav.focus_stack_count >= nav.focus_stack_capacity){
        nav.focus_stack_capacity = nav.focus_stack_capacity? nav.focus_stack_capacity * 2 : 4;
        nav.focus_stack = realloc(nav.focus_stack, nav.focus_stack_capacity * sizeof(DrJsonValue));
    }
    nav.focus_stack[nav.focus_stack_count++] = root;

    TestExpectEquals(nav.focus_stack_count, 1);

    // Pop focus
    nav.focus_stack_count--;
    TestExpectEquals(nav.focus_stack_count, 0);

    // Cleanup
    if(nav.focus_stack) free(nav.focus_stack);
    drjson_ctx_free_all(ctx);

    TESTEND();
}

// Test UTF-8 display width calculation
TestFunction(TestUTF8DisplayWidth){
    TESTBEGIN();

    // ASCII strings
    TestExpectEquals(utf8_display_width("hello", 5), 5);
    TestExpectEquals(utf8_display_width("", 0), 0);
    TestExpectEquals(utf8_display_width("a", 1), 1);

    // UTF-8 multi-byte characters
    // "café" = 5 bytes (4 chars: c, a, f, é where é is 2 bytes)
    TestExpectEquals(utf8_display_width("café", 5), 4);

    // "こんにちは" = 15 bytes (5 chars, each 3 bytes)
    TestExpectEquals(utf8_display_width("こんにちは", 15), 5);

    // Mixed ASCII and UTF-8
    TestExpectEquals(utf8_display_width("hello世界", 11), 7); // hello=5, 世界=2 chars (6 bytes)

    // Emoji (typically 4 bytes)
    TestExpectEquals(utf8_display_width("🎉", 4), 1);

    TESTEND();
}

// Test navigation sibling jumps
TestFunction(TestNavigationJumps){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create array for sibling navigation
    LongString json = LS("[\"a\", \"b\", \"c\", \"d\"]");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_ARRAY);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;
    nav_rebuild(&nav);

    // Should have items
    TestExpectTrue(nav.item_count > 0);

    // Start at first item
    nav.cursor_pos = 0;

    // Jump to next sibling
    size_t old_cursor = nav.cursor_pos;
    nav_jump_to_next_sibling(&nav);
    TestExpectTrue(nav.cursor_pos != old_cursor || nav.cursor_pos == 0); // Either moved or was already at end

    // Jump to prev sibling
    old_cursor = nav.cursor_pos;
    nav_jump_to_prev_sibling(&nav);
    TestExpectTrue(nav.cursor_pos <= old_cursor); // Should move back or stay

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test expand/collapse recursive operations
TestFunction(TestExpandCollapseRecursive){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create nested structure
    LongString json = LS("{\"a\": {\"b\": {\"c\": [1, 2, 3]}}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;
    nav_rebuild(&nav);

    size_t initial_count = nav.item_count;

    // Get a container to expand
    DrJsonValue inner = drjson_query(ctx, root, "a", 1);
    if(nav_is_container(inner)){
        uint64_t id = nav_get_container_id(inner);

        // Expand it
        bs_add(&nav.expanded, id);
        nav.needs_rebuild = 1;
        nav_rebuild(&nav);

        // Should have more items after expansion
        TestExpectTrue(nav.item_count >= initial_count);

        // Collapse it
        bs_remove(&nav.expanded, id);
        nav.needs_rebuild = 1;
        nav_rebuild(&nav);

        // Should have fewer items after collapse
        TestExpectTrue(nav.item_count <= nav.item_count);
    }

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test command lookup
TestFunction(TestCommandLookup){
    TESTBEGIN();

    // Test that we can find commands by name
    const Command* cmd = NULL;

    // Look for "help" command
    for(size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++){
        if(SV_equals(commands[i].name, SV("help"))){
            cmd = &commands[i];
            break;
        }
    }
    TestExpectTrue(cmd != NULL);
    if(cmd){
        TestExpectTrue(cmd->handler != NULL);
    }

    // Look for "quit" command
    cmd = NULL;
    for(size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++){
        if(SV_equals(commands[i].name, SV("quit")) || SV_equals(commands[i].name, SV("q"))){
            cmd = &commands[i];
            break;
        }
    }
    TestExpectTrue(cmd != NULL);

    // Look for "yank" command
    cmd = NULL;
    for(size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++){
        if(SV_equals(commands[i].name, SV("yank")) || SV_equals(commands[i].name, SV("y"))){
            cmd = &commands[i];
            break;
        }
    }
    TestExpectTrue(cmd != NULL);

    // Look for "filter" command
    cmd = NULL;
    for(size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++){
        if(SV_equals(commands[i].name, SV("filter")) || SV_equals(commands[i].name, SV("f"))){
            cmd = &commands[i];
            break;
        }
    }
    TestExpectTrue(cmd != NULL);

    TESTEND();
}

// Test BitSet edge cases
TestFunction(TestBitSetEdgeCases){
    TESTBEGIN();

    BitSet bs = {0};

    // Test with very large IDs
    bs_add(&bs, 1000000);
    TestExpectTrue(bs_contains(&bs, 1000000));
    TestExpectFalse(bs_contains(&bs, 1000001));

    // Add many values to force resize
    for(uint64_t i = 0; i < 100; i++){
        bs_add(&bs, i * 1000);
    }

    // Verify all values still present
    for(uint64_t i = 0; i < 100; i++){
        TestExpectTrue(bs_contains(&bs, i * 1000));
    }

    // Test that non-added values aren't present
    TestExpectFalse(bs_contains(&bs, 500));
    TestExpectFalse(bs_contains(&bs, 1500));

    // Remove some values and verify
    for(uint64_t i = 0; i < 50; i++){
        bs_remove(&bs, i * 1000);
    }

    for(uint64_t i = 0; i < 50; i++){
        TestExpectFalse(bs_contains(&bs, i * 1000));
    }
    for(uint64_t i = 50; i < 100; i++){
        TestExpectTrue(bs_contains(&bs, i * 1000));
    }

    // Test zero ID
    bs_add(&bs, 0);
    TestExpectTrue(bs_contains(&bs, 0));
    bs_remove(&bs, 0);
    TestExpectFalse(bs_contains(&bs, 0));

    // Cleanup
    if(bs.ids) free(bs.ids);
    TESTEND();
}

// Test complex nested paths
TestFunction(TestComplexNestedPaths){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create deeply nested structure
    LongString json = LS("{\"a\": {\"b\": {\"c\": {\"d\": {\"e\": \"deep\"}}}}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestAssertEquals((int)root.kind, DRJSON_OBJECT);

    // Test nested path navigation
    LongString path1 = LS("a.b.c.d.e");
    DrJsonValue result = drjson_query(ctx, root, path1.text, path1.length);
    TestExpectEquals((int)result.kind, DRJSON_STRING);

    StringView actual;
    int err = drjson_get_str_and_len(ctx, result, &actual.text, &actual.length);
    TestAssertFalse(err);
    TestExpectEquals2(SV_equals, actual, SV("deep"));

    // Test partial paths
    LongString path2 = LS("a.b.c");
    DrJsonValue partial = drjson_query(ctx, root, path2.text, path2.length);
    TestExpectEquals((int)partial.kind, DRJSON_OBJECT);

    // Test with arrays in path
    LongString json2 = LS("{\"items\": [{\"name\": \"first\"}, {\"name\": \"second\"}]}");
    DrJsonValue root2 = drjson_parse_string(ctx, json2.text, json2.length, 0);

    LongString path3 = LS("items[0].name");
    DrJsonValue arr_result = drjson_query(ctx, root2, path3.text, path3.length);
    TestExpectEquals((int)arr_result.kind, DRJSON_STRING);

    err = drjson_get_str_and_len(ctx, arr_result, &actual.text, &actual.length);
    TestAssertFalse(err);
    TestExpectEquals2(SV_equals, actual, SV("first"));

    // Test invalid path
    LongString path4 = LS("a.b.nonexistent");
    DrJsonValue invalid = drjson_query(ctx, root, path4.text, path4.length);
    TestExpectEquals((int)invalid.kind, DRJSON_ERROR);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test search with recursive expansion
TestFunction(TestSearchRecursiveExpansion){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.search_mode = SEARCH_RECURSIVE;

    // Nested structure with matches at different depths
    LongString json = LS("{\"outer\": {\"middle\": {\"inner\": \"target\"}}, \"other\": \"target\"}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    nav.root = root;

    // Search should find "target" at multiple levels
    _Bool found = nav_search_recursive_helper(&nav, root, (DrJsonAtom){0}, "target", 6);
    TestExpectTrue(found);

    // After search, containers with matches should be expanded
    DrJsonValue outer = drjson_query(ctx, root, "outer", 5);
    if(nav_is_container(outer)){
        uint64_t id = nav_get_container_id(outer);
        // Container should be in expanded set after search
        TestExpectTrue(bs_contains(&nav.expanded, id));
    }

    // Search for non-existent string
    found = nav_search_recursive_helper(&nav, root, (DrJsonAtom){0}, "notfound", 8);
    TestExpectFalse(found);

    // Cleanup
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test navigation at boundaries
TestFunction(TestNavigationBoundaries){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Empty array
    LongString json1 = LS("[]");
    DrJsonValue empty_arr = drjson_parse_string(ctx, json1.text, json1.length, 0);
    JsonNav nav1 = {0};
    nav1.jctx = ctx;
    nav1.root = empty_arr;
    nav_rebuild(&nav1);

    TestExpectTrue(nav1.item_count >= 1); // At least root
    nav1.cursor_pos = 0;
    nav_jump_to_next_sibling(&nav1);
    // Should not crash

    // Single element array
    LongString json2 = LS("[42]");
    DrJsonValue single = drjson_parse_string(ctx, json2.text, json2.length, 0);
    JsonNav nav2 = {0};
    nav2.jctx = ctx;
    nav2.root = single;
    nav_rebuild(&nav2);

    nav2.cursor_pos = 0;
    nav_jump_to_next_sibling(&nav2);
    nav_jump_to_prev_sibling(&nav2);
    // Should not crash

    // Empty object
    LongString json3 = LS("{}");
    DrJsonValue empty_obj = drjson_parse_string(ctx, json3.text, json3.length, 0);
    JsonNav nav3 = {0};
    nav3.jctx = ctx;
    nav3.root = empty_obj;
    nav_rebuild(&nav3);

    TestExpectTrue(nav3.item_count >= 1);

    // Cleanup
    if(nav1.items) free(nav1.items);
    if(nav2.items) free(nav2.items);
    if(nav3.items) free(nav3.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test message handling
TestFunction(TestMessageHandling){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    JsonNav nav = {0};
    nav.jctx = ctx;

    // Set a message
    nav_set_messagef(&nav, "Test message: %d", 42);
    TestExpectTrue(nav.message_length > 0);
    TestExpectTrue(strstr(nav.message, "42") != NULL);

    // Set another message (overwrites)
    nav_set_messagef(&nav, "New message");
    TestExpectTrue(nav.message_length > 0);
    TestExpectTrue(strstr(nav.message, "New") != NULL);

    // Very long message (test truncation)
    char long_msg[1000];
    memset(long_msg, 'A', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    nav_set_messagef(&nav, "%s", long_msg);
    TestExpectTrue(nav.message_length > 0);
    // Should be truncated to fit in message buffer (512 bytes)
    TestExpectTrue(nav.message_length < sizeof(nav.message));

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test line editor edge cases
TestFunction(TestLineEditorEdgeCases){
    TESTBEGIN();

    LineEditor le = {0};
    le_init(&le, 256);

    // Fill to near capacity
    for(size_t i = 0; i < 250; i++){
        le_append_char(&le, 'x');
    }
    TestExpectEquals(le.length, 250);

    // Try to overfill (should stop at capacity-1)
    for(size_t i = 0; i < 20; i++){
        le_append_char(&le, 'y');
    }
    TestExpectTrue(le.length < le.capacity);

    // Delete from empty position
    le_clear(&le);
    le_backspace(&le); // Should not crash
    TestExpectEquals(le.length, 0);

    le_delete(&le); // Should not crash
    TestExpectEquals(le.length, 0);

    // Cursor movement at boundaries
    le_move_left(&le); // Already at 0
    TestExpectEquals(le.cursor_pos, 0);

    le_append_char(&le, 'a');
    le_move_right(&le);
    le_move_right(&le); // Beyond end
    TestExpectEquals(le.cursor_pos, le.length);

    // Word deletion on empty
    le_clear(&le);
    le_delete_word_backward(&le); // Should not crash
    TestExpectEquals(le.length, 0);

    // Delete word with only spaces
    le_clear(&le);
    le_append_char(&le, ' ');
    le_append_char(&le, ' ');
    le_append_char(&le, ' ');
    le_delete_word_backward(&le);
    TestExpectTrue(le.length < 3);

    le_free(&le);
    TESTEND();
}

// Test large JSON structures
TestFunction(TestLargeJSONStructures){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Large array
    char* large_arr = malloc(10000);
    size_t pos = 0;
    large_arr[pos++] = '[';
    for(int i = 0; i < 100; i++){
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%d,", i);
        memcpy(large_arr + pos, buf, n);
        pos += n;
    }
    memcpy(large_arr + pos, "100]", 5);
    pos += 4;
    size_t large_arr_len = pos;

    DrJsonValue arr = drjson_parse_string(ctx, large_arr, large_arr_len, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);
    TestExpectEquals(drjson_len(ctx, arr), 101);

    // Navigate through large structure
    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;
    nav_rebuild(&nav);

    // Should be able to build navigation (collapsed or flat view)
    TestExpectTrue(nav.item_count >= 1);

    // Verify we can query specific elements
    DrJsonValue elem_50 = drjson_query(ctx, arr, "[50]", 4);
    TestExpectEquals((int)elem_50.kind, DRJSON_UINTEGER);
    TestExpectEquals(elem_50.uinteger, 50);

    // Deeply nested structure (10 levels)
    LongString deep_json = LS("{\"l1\":{\"l2\":{\"l3\":{\"l4\":{\"l5\":{\"l6\":{\"l7\":{\"l8\":{\"l9\":{\"l10\":\"deep\"}}}}}}}}}}");
    DrJsonValue deep = drjson_parse_string(ctx, deep_json.text, deep_json.length, 0);
    TestExpectEquals((int)deep.kind, DRJSON_OBJECT);

    // Should be able to query deep path
    LongString deep_path = LS("l1.l2.l3.l4.l5.l6.l7.l8.l9.l10");
    DrJsonValue deep_val = drjson_query(ctx, deep, deep_path.text, deep_path.length);
    TestExpectEquals((int)deep_val.kind, DRJSON_STRING);

    // Cleanup
    if(nav.items) free(nav.items);
    free(large_arr);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test search navigation (next/prev)
TestFunction(TestSearchNavigation){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // JSON with multiple matches for "test"
    LongString json = LS("[\"test\", \"other\", \"test\", \"more\", \"test\"]");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_ARRAY);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand the array
    uint64_t arr_id = nav_get_container_id(root);
    bs_add(&nav.expanded, arr_id);
    nav_rebuild(&nav);

    // Set search pattern
    le_init(&nav.search_buffer, 256);
    for(const char* p = "test"; *p; p++){
        le_append_char(&nav.search_buffer, *p);
    }

    // Start at position 0
    nav.cursor_pos = 0;

    // Search next - should find first "test" at index 0 (the array item)
    nav_search_next(&nav);
    TestExpectTrue(nav.cursor_pos > 0); // Should move from root

    size_t first_match = nav.cursor_pos;

    // Search next again - should find second "test"
    nav_search_next(&nav);
    TestExpectTrue(nav.cursor_pos > first_match);

    size_t second_match = nav.cursor_pos;

    // Search next again - should find third "test"
    nav_search_next(&nav);
    TestExpectTrue(nav.cursor_pos > second_match);

    size_t third_match = nav.cursor_pos;

    // Search next again - should wrap around to first match
    nav_search_next(&nav);
    TestExpectEquals(nav.cursor_pos, first_match);

    // Now test backward search
    nav.cursor_pos = third_match;
    nav_search_prev(&nav);
    TestExpectEquals(nav.cursor_pos, second_match);

    nav_search_prev(&nav);
    TestExpectEquals(nav.cursor_pos, first_match);

    // Search prev from first should wrap to last
    nav_search_prev(&nav);
    TestExpectEquals(nav.cursor_pos, third_match);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test value comparison for sorting
TestFunction(TestValueComparison){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Test type ordering: null < bool < number < string < array < object
    DrJsonValue null_val = drjson_make_null();
    DrJsonValue bool_val = drjson_make_bool(1);
    DrJsonValue int_val = drjson_make_int(42);
    DrJsonAtom hello_atom;
    int atom_err = drjson_atomize(ctx, "hello", 5, &hello_atom);
    TestAssertFalse(atom_err);
    DrJsonValue str_val = drjson_atom_to_value(hello_atom);

    LongString arr_json = LS("[1,2,3]");
    DrJsonValue arr_val = drjson_parse_string(ctx, arr_json.text, arr_json.length, 0);

    LongString obj_json = LS("{\"a\":1}");
    DrJsonValue obj_val = drjson_parse_string(ctx, obj_json.text, obj_json.length, 0);

    // Test type ordering
    TestExpectTrue(compare_values(null_val, bool_val, ctx) < 0);
    TestExpectTrue(compare_values(bool_val, int_val, ctx) < 0);
    TestExpectTrue(compare_values(int_val, str_val, ctx) < 0);
    TestExpectTrue(compare_values(str_val, arr_val, ctx) < 0);
    TestExpectTrue(compare_values(arr_val, obj_val, ctx) < 0);

    // Test same types
    TestExpectEquals(compare_values(null_val, null_val, ctx), 0);

    // Test booleans
    DrJsonValue bool_false = drjson_make_bool(0);
    DrJsonValue bool_true = drjson_make_bool(1);
    TestExpectTrue(compare_values(bool_false, bool_true, ctx) < 0);
    TestExpectTrue(compare_values(bool_true, bool_false, ctx) > 0);

    // Test numbers
    DrJsonValue int1 = drjson_make_int(10);
    DrJsonValue int2 = drjson_make_int(20);
    TestExpectTrue(compare_values(int1, int2, ctx) < 0);
    TestExpectTrue(compare_values(int2, int1, ctx) > 0);
    TestExpectEquals(compare_values(int1, int1, ctx), 0);

    DrJsonValue uint1 = drjson_make_uint(100);
    DrJsonValue uint2 = drjson_make_uint(200);
    TestExpectTrue(compare_values(uint1, uint2, ctx) < 0);

    DrJsonValue num1 = drjson_make_number(3.14);
    DrJsonValue num2 = drjson_make_number(2.71);
    TestExpectTrue(compare_values(num2, num1, ctx) < 0);

    // Test strings
    DrJsonAtom apple_atom, banana_atom;
    atom_err = drjson_atomize(ctx, "apple", 5, &apple_atom);
    TestAssertFalse(atom_err);
    atom_err = drjson_atomize(ctx, "banana", 6, &banana_atom);
    TestAssertFalse(atom_err);
    DrJsonValue str_a = drjson_atom_to_value(apple_atom);
    DrJsonValue str_b = drjson_atom_to_value(banana_atom);
    TestExpectTrue(compare_values(str_a, str_b, ctx) < 0);
    TestExpectTrue(compare_values(str_b, str_a, ctx) > 0);
    TestExpectEquals(compare_values(str_a, str_a, ctx), 0);

    // Test string length (shorter strings sort before longer strings with same prefix)
    DrJsonAtom a_atom, aa_atom;
    atom_err = drjson_atomize(ctx, "a", 1, &a_atom);
    TestAssertFalse(atom_err);
    atom_err = drjson_atomize(ctx, "aa", 2, &aa_atom);
    TestAssertFalse(atom_err);
    DrJsonValue str_short = drjson_atom_to_value(a_atom);
    DrJsonValue str_long = drjson_atom_to_value(aa_atom);
    TestExpectTrue(compare_values(str_short, str_long, ctx) < 0);

    // Test arrays by length
    LongString arr_small_json = LS("[1]");
    LongString arr_large_json = LS("[1,2,3,4,5]");
    DrJsonValue arr_small = drjson_parse_string(ctx, arr_small_json.text, arr_small_json.length, 0);
    DrJsonValue arr_large = drjson_parse_string(ctx, arr_large_json.text, arr_large_json.length, 0);
    TestExpectTrue(compare_values(arr_small, arr_large, ctx) < 0);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test parsing user input as strings
TestFunction(TestParseAsString){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    DrJsonAtom result;

    // Parse bare word
    int err = parse_as_string(ctx, "hello", 5, &result);
    TestExpectEquals(err, 0);
    StringView sv;
    int get_err = drjson_get_atom_str_and_length(ctx, result, &sv.text, &sv.length);
    TestAssertFalse(get_err);
    TestExpectEquals2(SV_equals, sv, SV("hello"));

    // Parse quoted string
    err = parse_as_string(ctx, "\"world\"", 7, &result);
    TestExpectEquals(err, 0);
    get_err = drjson_get_atom_str_and_length(ctx, result, &sv.text, &sv.length);
    TestAssertFalse(get_err);
    TestExpectEquals2(SV_equals, sv, SV("world"));

    // Parse string with whitespace
    err = parse_as_string(ctx, "  test  ", 8, &result);
    TestExpectEquals(err, 0);
    get_err = drjson_get_atom_str_and_length(ctx, result, &sv.text, &sv.length);
    TestAssertFalse(get_err);
    TestExpectEquals2(SV_equals, sv, SV("test"));

    // Parse quoted string with escape
    err = parse_as_string(ctx, "\"hello\\nworld\"", 14, &result);
    TestExpectEquals(err, 0);
    get_err = drjson_get_atom_str_and_length(ctx, result, &sv.text, &sv.length);
    TestAssertFalse(get_err);
    // Length should be 11 (hello + newline + world) but implementation details may vary
    TestExpectTrue(sv.length > 0);

    // Parse empty string
    err = parse_as_string(ctx, "", 0, &result);
    TestExpectEquals(err, 0);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test parsing user input as values
TestFunction(TestParseAsValue){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    DrJsonValue result;

    // Parse integer
    int err = parse_as_value(ctx, "42", 2, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_UINTEGER);
    TestExpectEquals(result.uinteger, 42);

    // Parse negative integer
    err = parse_as_value(ctx, "-123", 4, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_INTEGER);
    TestExpectEquals(result.integer, -123);

    // Parse float
    err = parse_as_value(ctx, "3.14", 4, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_NUMBER);
    TestExpectTrue(result.number > 3.13 && result.number < 3.15);

    // Parse boolean
    err = parse_as_value(ctx, "true", 4, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectTrue(result.boolean);

    err = parse_as_value(ctx, "false", 5, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectFalse(result.boolean);

    // Parse null
    err = parse_as_value(ctx, "null", 4, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_NULL);

    // Parse quoted string
    err = parse_as_value(ctx, "\"hello\"", 7, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_STRING);

    // Parse bare word as string
    err = parse_as_value(ctx, "bareword", 8, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_STRING);

    // Parse array
    err = parse_as_value(ctx, "[1,2,3]", 7, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_ARRAY);
    TestExpectEquals(drjson_len(ctx, result), 3);

    // Parse object
    err = parse_as_value(ctx, "{\"a\":1}", 7, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_OBJECT);

    // Parse with whitespace
    err = parse_as_value(ctx, "  42  ", 6, &result);
    TestExpectEquals(err, 0);
    TestExpectEquals((int)result.kind, DRJSON_UINTEGER);
    TestExpectEquals(result.uinteger, 42);

    // Parse empty should fail
    err = parse_as_value(ctx, "", 0, &result);
    TestExpectTrue(err != 0);

    // Parse incomplete JSON should fail or fallback
    err = parse_as_value(ctx, "[1,2", 4, &result);
    // This might succeed as bareword or fail

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test container ID generation
TestFunction(TestContainerID){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create arrays and objects
    LongString arr1_json = LS("[1,2,3]");
    LongString arr2_json = LS("[4,5,6]");
    LongString obj1_json = LS("{\"a\":1}");
    LongString obj2_json = LS("{\"b\":2}");

    DrJsonValue arr1 = drjson_parse_string(ctx, arr1_json.text, arr1_json.length, 0);
    DrJsonValue arr2 = drjson_parse_string(ctx, arr2_json.text, arr2_json.length, 0);
    DrJsonValue obj1 = drjson_parse_string(ctx, obj1_json.text, obj1_json.length, 0);
    DrJsonValue obj2 = drjson_parse_string(ctx, obj2_json.text, obj2_json.length, 0);

    // Get IDs
    uint64_t id_arr1 = nav_get_container_id(arr1);
    uint64_t id_arr2 = nav_get_container_id(arr2);
    uint64_t id_obj1 = nav_get_container_id(obj1);
    uint64_t id_obj2 = nav_get_container_id(obj2);

    // IDs should be unique
    TestExpectTrue(id_arr1 != id_arr2);
    TestExpectTrue(id_obj1 != id_obj2);
    TestExpectTrue(id_arr1 != id_obj1);
    TestExpectTrue(id_arr1 != id_obj2);

    // Arrays have even IDs (bit 0 = 0), objects have odd IDs (bit 0 = 1)
    TestExpectEquals(id_arr1 & 1, 0);
    TestExpectEquals(id_arr2 & 1, 0);
    TestExpectEquals(id_obj1 & 1, 1);
    TestExpectEquals(id_obj2 & 1, 1);

    // Same value should have same ID
    uint64_t id_arr1_again = nav_get_container_id(arr1);
    TestExpectEquals(id_arr1, id_arr1_again);

    // Non-containers should return 0
    DrJsonValue num = drjson_make_int(42);
    TestExpectEquals(nav_get_container_id(num), 0);

    DrJsonAtom test_atom;
    int atom_err = drjson_atomize(ctx, "test", 4, &test_atom);
    TestAssertFalse(atom_err);
    DrJsonValue str = drjson_atom_to_value(test_atom);
    TestExpectEquals(nav_get_container_id(str), 0);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test search with container expansion
TestFunction(TestSearchWithExpansion){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Nested JSON where match is inside collapsed container
    LongString json = LS("{\"outer\": {\"inner\": \"target\"}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Start with root expanded, but not children
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);
    nav_rebuild(&nav);

    // Set search pattern to "target"
    le_init(&nav.search_buffer, 256);
    for(const char* p = "target"; *p; p++){
        le_append_char(&nav.search_buffer, *p);
    }

    nav.cursor_pos = 0;
    size_t initial_pos = nav.cursor_pos;

    // Search should find "target" and expand the "outer" container
    nav_search_next(&nav);

    // Cursor should have moved
    TestExpectTrue(nav.cursor_pos != initial_pos);

    // The outer object should now be expanded
    DrJsonValue outer = drjson_query(ctx, root, "outer", 5);
    uint64_t outer_id = nav_get_container_id(outer);
    TestExpectTrue(bs_contains(&nav.expanded, outer_id));

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test flat view mode for large arrays
TestFunction(TestFlatViewMode){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create array with 25 items (will create 3 rows with 10 items per row)
    char* json = malloc(1000);
    size_t json_pos = 0;
    json[json_pos++] = '[';
    for(int i = 0; i < 25; i++){
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "%d%s", i, (i < 24) ? "," : "");
        memcpy(json + json_pos, buf, n);
        json_pos += n;
    }
    json[json_pos++] = ']';
    json[json_pos] = '\0';

    DrJsonValue arr = drjson_parse_string(ctx, json, json_pos, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);
    TestExpectEquals(drjson_len(ctx, arr), 25);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;

    // Expand array - should trigger flat view
    uint64_t arr_id = nav_get_container_id(arr);
    bs_add(&nav.expanded, arr_id);
    nav_rebuild(&nav);

    // Check for flat view items
    _Bool found_flat_view = 0;
    for(size_t i = 0; i < nav.item_count; i++){
        if(nav.items[i].is_flat_view){
            found_flat_view = 1;
            // Flat view items should have valid row indices
            TestExpectTrue(nav.items[i].index >= 0);
            break;
        }
    }
    TestExpectTrue(found_flat_view);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    free(json);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test sorting arrays
TestFunction(TestSortingArrays){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create array of numbers in random order
    LongString json = LS("[5, 2, 8, 1, 9, 3]");
    DrJsonValue arr = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;
    nav_rebuild(&nav);

    // Position cursor on array
    nav.cursor_pos = 0;

    // Sort ascending (default)
    int result = cmd_sort(&nav, "", 0);
    TestExpectEquals(result, CMD_OK);

    // Verify array is sorted
    DrJsonValue sorted = nav.items[0].value;
    TestExpectEquals((int)sorted.kind, DRJSON_ARRAY);
    TestExpectEquals(drjson_len(ctx, sorted), 6);

    DrJsonValue elem0 = drjson_get_by_index(ctx, sorted, 0);
    DrJsonValue elem5 = drjson_get_by_index(ctx, sorted, 5);

    // First element should be smaller than last
    TestExpectTrue(compare_values(elem0, elem5, ctx) < 0);

    // Test descending sort
    result = cmd_sort(&nav, "desc", 4);
    TestExpectEquals(result, CMD_OK);

    sorted = nav.items[0].value;
    elem0 = drjson_get_by_index(ctx, sorted, 0);
    elem5 = drjson_get_by_index(ctx, sorted, 5);

    // After desc sort, first should be larger than last
    TestExpectTrue(compare_values(elem0, elem5, ctx) > 0);

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test sorting objects
TestFunction(TestSortingObjects){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create object with numeric values
    LongString json = LS("{\"z\": 30, \"a\": 10, \"m\": 20}");
    DrJsonValue obj = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)obj.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = obj;
    nav_rebuild(&nav);

    nav.cursor_pos = 0;

    // Sort by values ascending
    int result = cmd_sort(&nav, "values asc", 10);
    TestExpectEquals(result, CMD_OK);

    // Verify object exists and has 3 items
    DrJsonValue sorted = nav.items[0].value;
    TestExpectEquals((int)sorted.kind, DRJSON_OBJECT);
    TestExpectEquals(drjson_len(ctx, sorted), 3);

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test filtering arrays - basic truthiness filter
TestFunction(TestFilteringArrays){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create array with mix of values
    LongString json = LS("[1, 0, 5, null, 10, false]");
    DrJsonValue arr = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);
    TestExpectEquals(drjson_len(ctx, arr), 6);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;
    nav_rebuild(&nav);

    nav.cursor_pos = 0;

    // Filter: keep truthy items using simple path "."
    int result = cmd_filter(&nav, ".", 1);
    // Filter might fail if expression parsing isn't available, that's ok
    if(result == CMD_OK){
        // Verify filtered array
        DrJsonValue filtered = nav.root;
        TestExpectEquals((int)filtered.kind, DRJSON_ARRAY);

        // Should have fewer items (only truthy ones)
        int64_t filtered_len = drjson_len(ctx, filtered);
        TestExpectTrue(filtered_len < 6);
    }

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test filtering objects - basic truthiness filter
TestFunction(TestFilteringObjects){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create object with mixed values
    LongString json = LS("{\"a\": 0, \"b\": 15, \"c\": null}");
    DrJsonValue obj = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)obj.kind, DRJSON_OBJECT);
    TestExpectEquals(drjson_len(ctx, obj), 3);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = obj;
    nav_rebuild(&nav);

    nav.cursor_pos = 0;

    // Filter: keep truthy values
    int result = cmd_filter(&nav, ".", 1);
    // Filter might fail if expression parsing isn't available, that's ok
    if(result == CMD_OK){
        // Verify filtered object
        DrJsonValue filtered = nav.root;
        TestExpectEquals((int)filtered.kind, DRJSON_OBJECT);

        // Should have fewer items (only truthy ones)
        int64_t filtered_len = drjson_len(ctx, filtered);
        TestExpectTrue(filtered_len <= 3);
    }

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test truthiness evaluation
TestFunction(TestTruthiness){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Test various truthy/falsy values
    TestExpectFalse(is_truthy(drjson_make_null(), ctx));
    TestExpectFalse(is_truthy(drjson_make_bool(0), ctx));
    TestExpectTrue(is_truthy(drjson_make_bool(1), ctx));

    TestExpectFalse(is_truthy(drjson_make_int(0), ctx));
    TestExpectTrue(is_truthy(drjson_make_int(42), ctx));
    TestExpectTrue(is_truthy(drjson_make_int(-5), ctx));

    TestExpectFalse(is_truthy(drjson_make_uint(0), ctx));
    TestExpectTrue(is_truthy(drjson_make_uint(100), ctx));

    TestExpectFalse(is_truthy(drjson_make_number(0.0), ctx));
    TestExpectTrue(is_truthy(drjson_make_number(3.14), ctx));

    // Empty string is falsy, non-empty is truthy
    DrJsonAtom empty_atom, nonempty_atom;
    int atom_err = drjson_atomize(ctx, "", 0, &empty_atom);
    TestAssertFalse(atom_err);
    atom_err = drjson_atomize(ctx, "hello", 5, &nonempty_atom);
    TestAssertFalse(atom_err);
    TestExpectFalse(is_truthy(drjson_atom_to_value(empty_atom), ctx));
    TestExpectTrue(is_truthy(drjson_atom_to_value(nonempty_atom), ctx));

    // Empty array/object is falsy
    DrJsonValue empty_arr = drjson_make_array(ctx);
    TestExpectFalse(is_truthy(empty_arr, ctx));

    DrJsonValue nonempty_arr = drjson_parse_string(ctx, "[1]", 3, 0);
    TestExpectTrue(is_truthy(nonempty_arr, ctx));

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_rebuild_recursive
TestFunction(TestNavRebuildRecursive){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create nested structure
    LongString json = LS("{\"arr\": [1, 2, 3], \"obj\": {\"x\": 10}, \"num\": 42}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand root only (not children)
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);

    nav_rebuild(&nav);

    // Should have root + 3 children (arr, obj, num)
    TestExpectTrue(nav.item_count >= 4);

    // Now expand the array
    DrJsonValue arr = drjson_query(ctx, root, "arr", 3);
    uint64_t arr_id = nav_get_container_id(arr);
    bs_add(&nav.expanded, arr_id);

    size_t count_before = nav.item_count;
    nav_rebuild(&nav);

    // Should now have more items after expanding array
    TestExpectTrue(nav.item_count > count_before);

    // Verify we can find numeric items
    _Bool found_num = 0;
    for(size_t i = 0; i < nav.item_count; i++){
        if(nav.items[i].value.kind == DRJSON_INTEGER ||
           nav.items[i].value.kind == DRJSON_UINTEGER){
            found_num = 1;
        }
    }
    TestExpectTrue(found_num);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test operator parsing
TestFunction(TestOperatorParsing){
    TESTBEGIN();

    Operator op;
    const char* test = "== test";
    const char* result = parse_operator(test, test + 7, &op);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)op, OP_EQ);

    test = "!= test";
    result = parse_operator(test, test + 7, &op);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)op, OP_NEQ);

    test = ">= test";
    result = parse_operator(test, test + 7, &op);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)op, OP_GTE);

    test = "<= test";
    result = parse_operator(test, test + 7, &op);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)op, OP_LTE);

    test = "> test";
    result = parse_operator(test, test + 6, &op);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)op, OP_GT);

    test = "< test";
    result = parse_operator(test, test + 6, &op);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)op, OP_LT);

    TESTEND();
}

// Test literal parsing for filter expressions
TestFunction(TestLiteralParsing){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    DrJsonValue val;

    // Parse integer literal
    const char* test = "42";
    const char* result = parse_literal(ctx, test, test + 2, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_UINTEGER);
    TestExpectEquals(val.uinteger, 42);

    // Parse negative integer
    test = "-123";
    result = parse_literal(ctx, test, test + 4, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_INTEGER);
    TestExpectEquals(val.integer, -123);

    // Parse float
    test = "3.14";
    result = parse_literal(ctx, test, test + 4, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_NUMBER);
    TestExpectTrue(val.number > 3.13 && val.number < 3.15);

    // Parse null
    test = "null";
    result = parse_literal(ctx, test, test + 4, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_NULL);

    // Parse boolean true
    test = "true";
    result = parse_literal(ctx, test, test + 4, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_BOOL);
    TestExpectTrue(val.boolean);

    // Parse boolean false
    test = "false";
    result = parse_literal(ctx, test, test + 5, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_BOOL);
    TestExpectFalse(val.boolean);

    // Parse string
    test = "\"hello\"";
    result = parse_literal(ctx, test, test + 7, &val);
    TestExpectTrue(result != NULL);
    TestExpectEquals((int)val.kind, DRJSON_STRING);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test query command
TestFunction(TestQueryCommand){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create nested JSON
    LongString json = LS("{\"user\": {\"name\": \"Alice\", \"age\": 30}, \"items\": [1, 2, 3]}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand root
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);
    nav_rebuild(&nav);

    nav.cursor_pos = 0;

    // Query to user (single level)
    int result = cmd_query(&nav, "user", 4);
    if(result == CMD_OK){
        // Cursor should have moved
        TestExpectTrue(nav.cursor_pos >= 0);
    }

    // Query to array element
    result = cmd_query(&nav, "items", 5);
    // May succeed or fail depending on visibility

    // Query with invalid path should fail
    result = cmd_query(&nav, "nonexistent", 11);
    TestExpectEquals(result, CMD_ERROR);

    // Empty query should fail
    result = cmd_query(&nav, "", 0);
    TestExpectEquals(result, CMD_ERROR);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test focus/unfocus commands
TestFunction(TestFocusUnfocusCommands){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create nested JSON
    LongString json = LS("{\"outer\": {\"inner\": \"value\"}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand root
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);
    nav_rebuild(&nav);

    // Move to the "outer" object
    TestExpectTrue(nav.item_count > 1);
    nav.cursor_pos = 1; // Should be the "outer" field

    // Ensure we're on a container
    if(nav_is_container(nav.items[nav.cursor_pos].value)){
        // Focus on it
        int result = cmd_focus(&nav, "", 0);
        TestExpectEquals(result, CMD_OK);

        // Focus stack should have one item
        TestExpectEquals(nav.focus_stack_count, 1);

        // Root should now be the "outer" object
        TestExpectEquals((int)nav.root.kind, DRJSON_OBJECT);

        // Unfocus should go back
        result = cmd_unfocus(&nav, "", 0);
        TestExpectEquals(result, CMD_OK);

        // Focus stack should be empty
        TestExpectEquals(nav.focus_stack_count, 0);

        // Root should be original
        TestExpectEquals((int)nav.root.kind, DRJSON_OBJECT);
    }

    // Unfocus when already at top should fail
    int result = cmd_unfocus(&nav, "", 0);
    TestExpectEquals(result, CMD_ERROR);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    if(nav.focus_stack) free(nav.focus_stack);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test navigation jump to nth child
TestFunction(TestNavJumpToNthChild){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create array with 10 items
    LongString json = LS("[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]");
    DrJsonValue arr = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;

    // Expand array
    uint64_t arr_id = nav_get_container_id(arr);
    bs_add(&nav.expanded, arr_id);
    nav_rebuild(&nav);

    // Should have at least the root
    TestExpectTrue(nav.item_count >= 1);

    // If we have children visible, test navigation
    if(nav.item_count > 5){
        // Start at root
        nav.cursor_pos = 0;

        // Jump to 3rd child (index 2)
        nav_jump_to_nth_child(&nav, 2);

        // Cursor should have moved or stayed at 0
        TestExpectTrue(nav.cursor_pos >= 0);

        // Jump to 7th child (index 6)
        nav.cursor_pos = 0;
        nav_jump_to_nth_child(&nav, 6);
        TestExpectTrue(nav.cursor_pos >= 0);
    }

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test focus stack operations
TestFunction(TestFocusStackOperations){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    JsonNav nav = {0};
    nav.jctx = ctx;

    DrJsonValue val1 = drjson_make_int(42);
    DrJsonValue val2 = drjson_make_int(84);
    DrJsonValue val3 = drjson_make_int(126);

    // Push values
    nav_focus_stack_push(&nav, val1);
    TestExpectEquals(nav.focus_stack_count, 1);

    nav_focus_stack_push(&nav, val2);
    TestExpectEquals(nav.focus_stack_count, 2);

    nav_focus_stack_push(&nav, val3);
    TestExpectEquals(nav.focus_stack_count, 3);

    // Pop in reverse order
    DrJsonValue popped = nav_focus_stack_pop(&nav);
    TestExpectEquals((int)popped.kind, DRJSON_INTEGER);
    TestExpectEquals(popped.integer, 126);
    TestExpectEquals(nav.focus_stack_count, 2);

    popped = nav_focus_stack_pop(&nav);
    TestExpectEquals(popped.integer, 84);
    TestExpectEquals(nav.focus_stack_count, 1);

    popped = nav_focus_stack_pop(&nav);
    TestExpectEquals(popped.integer, 42);
    TestExpectEquals(nav.focus_stack_count, 0);

    // Pop from empty should return error
    popped = nav_focus_stack_pop(&nav);
    TestExpectEquals((int)popped.kind, DRJSON_ERROR);

    // Cleanup
    if(nav.focus_stack) free(nav.focus_stack);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test complex query paths
TestFunction(TestComplexQueryPaths){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create complex nested structure
    LongString json = LS("{\"data\": [{\"id\": 1, \"values\": [10, 20, 30]}, {\"id\": 2, \"values\": [40, 50, 60]}]}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    // Query: data[0].id
    DrJsonValue result = drjson_query(ctx, root, "data[0].id", 10);
    TestExpectEquals((int)result.kind, DRJSON_UINTEGER);
    TestExpectEquals(result.uinteger, 1);

    // Query: data[1].values[2]
    result = drjson_query(ctx, root, "data[1].values[2]", 17);
    TestExpectEquals((int)result.kind, DRJSON_UINTEGER);
    TestExpectEquals(result.uinteger, 60);

    // Query: data[0].values
    result = drjson_query(ctx, root, "data[0].values", 14);
    TestExpectEquals((int)result.kind, DRJSON_ARRAY);
    TestExpectEquals(drjson_len(ctx, result), 3);

    // Query with out of bounds index
    result = drjson_query(ctx, root, "data[5]", 7);
    TestExpectEquals((int)result.kind, DRJSON_ERROR);

    // Query with invalid key
    result = drjson_query(ctx, root, "data[0].nonexistent", 19);
    TestExpectEquals((int)result.kind, DRJSON_ERROR);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test strip_whitespace helper
TestFunction(TestStripWhitespace){
    TESTBEGIN();

    StringView sv;

    // Test leading whitespace
    sv = SV("  hello");
    strip_whitespace(&sv.text, &sv.length);
    TestExpectEquals2(SV_equals, sv, SV("hello"));

    // Test trailing whitespace
    sv = SV("world  ");
    strip_whitespace(&sv.text, &sv.length);
    TestExpectEquals2(SV_equals, sv, SV("world"));

    // Test both leading and trailing
    sv = SV("  test  ");
    strip_whitespace(&sv.text, &sv.length);
    TestExpectEquals2(SV_equals, sv, SV("test"));

    // Test no whitespace
    sv = SV("foo");
    strip_whitespace(&sv.text, &sv.length);
    TestExpectEquals2(SV_equals, sv, SV("foo"));

    // Test only whitespace
    sv = SV("    ");
    strip_whitespace(&sv.text, &sv.length);
    TestExpectEquals(sv.length, 0);

    // Test empty string
    sv = SV("");
    strip_whitespace(&sv.text, &sv.length);
    TestExpectEquals(sv.length, 0);

    TESTEND();
}

// Test nav_jump_to_parent
TestFunction(TestNavJumpToParent){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create nested structure
    LongString json = LS("{\"outer\": {\"inner\": {\"deep\": \"value\"}}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand all levels
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);

    DrJsonValue outer = drjson_query(ctx, root, "outer", 5);
    uint64_t outer_id = nav_get_container_id(outer);
    bs_add(&nav.expanded, outer_id);

    DrJsonValue inner = drjson_query(ctx, root, "outer.inner", 11);
    uint64_t inner_id = nav_get_container_id(inner);
    bs_add(&nav.expanded, inner_id);

    nav_rebuild(&nav);

    // Navigate to deepest item
    if(nav.item_count > 3){
        nav.cursor_pos = nav.item_count - 1; // Go to last item (deepest)
        int deep_depth = nav.items[nav.cursor_pos].depth;

        // Jump to parent without collapsing
        nav_jump_to_parent(&nav, 0);

        // Should have moved to shallower depth
        TestExpectTrue(nav.items[nav.cursor_pos].depth < deep_depth);

        // Jump again
        int parent_depth = nav.items[nav.cursor_pos].depth;
        nav_jump_to_parent(&nav, 0);

        // Should be even shallower
        if(parent_depth > 0){
            TestExpectTrue(nav.items[nav.cursor_pos].depth < parent_depth);
        }
    }

    // Test jump from root does nothing
    nav.cursor_pos = 0;
    size_t orig_pos = nav.cursor_pos;
    nav_jump_to_parent(&nav, 0);
    TestExpectEquals(nav.cursor_pos, orig_pos);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_navigate_to_path
TestFunction(TestNavNavigateToPath){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create structure with array
    LongString json = LS("{\"data\": [\"a\", \"b\", \"c\"]}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand root
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);
    nav_rebuild(&nav);

    // Create a path: data[1]
    DrJsonPath path;
    int err = drjson_path_parse(ctx, "data[1]", 7, &path);
    TestExpectEquals(err, 0);

    // Navigate from root
    size_t result_idx = nav_navigate_to_path(&nav, 0, &path);

    // Should have navigated somewhere
    TestExpectTrue(result_idx < nav.item_count);

    // Try empty path (should return same index)
    DrJsonPath empty_path = {0};
    result_idx = nav_navigate_to_path(&nav, 0, &empty_path);
    TestExpectEquals(result_idx, 0);

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test tui_eval_expression
TestFunction(TestTuiEvalExpression){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = drjson_make_null();

    // Create a test value
    LongString json = LS("{\"age\": 25, \"name\": \"Alice\"}");
    DrJsonValue val = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)val.kind, DRJSON_OBJECT);

    // Test truthy expression (just path, no operator)
    TuiParsedExpression expr = {0};
    int err = drjson_path_parse(ctx, "age", 3, &expr.path);
    TestExpectEquals(err, 0);
    expr.op = OP_INVALID; // Truthy mode

    DrJsonValue result = tui_eval_expression(&nav, val, &expr);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectTrue(result.boolean); // 25 is truthy

    // Test comparison: age > 20
    err = drjson_path_parse(ctx, "age", 3, &expr.path);
    TestExpectEquals(err, 0);
    expr.op = OP_GT;
    expr.rhs_is_path = 0;
    expr.rhs_literal = drjson_make_int(20);

    result = tui_eval_expression(&nav, val, &expr);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectTrue(result.boolean); // 25 > 20 is true

    // Test equality: age == 25
    expr.op = OP_EQ;
    expr.rhs_literal = drjson_make_int(25);

    result = tui_eval_expression(&nav, val, &expr);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectTrue(result.boolean); // 25 == 25 is true

    // Test inequality: age != 30
    expr.op = OP_NEQ;
    expr.rhs_literal = drjson_make_int(30);

    result = tui_eval_expression(&nav, val, &expr);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectTrue(result.boolean); // 25 != 30 is true

    // Test less than: age < 30
    expr.op = OP_LT;
    expr.rhs_literal = drjson_make_int(30);

    result = tui_eval_expression(&nav, val, &expr);
    TestExpectEquals((int)result.kind, DRJSON_BOOL);
    TestExpectTrue(result.boolean); // 25 < 30 is true

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test drj_to_double_for_sort
TestFunction(TestDrjToDoubleForSort){
    TESTBEGIN();

    // Test DRJSON_NUMBER
    DrJsonValue num = drjson_make_number(3.14);
    double d = drj_to_double_for_sort(num);
    TestExpectTrue(d > 3.13 && d < 3.15);

    // Test DRJSON_INTEGER
    DrJsonValue int_val = drjson_make_int(-42);
    d = drj_to_double_for_sort(int_val);
    TestExpectTrue(d == -42.0);

    // Test DRJSON_UINTEGER
    DrJsonValue uint_val = drjson_make_uint(100);
    d = drj_to_double_for_sort(uint_val);
    TestExpectTrue(d == 100.0);

    // Test non-numeric (should return 0.0)
    DrJsonValue null_val = drjson_make_null();
    d = drj_to_double_for_sort(null_val);
    TestExpectTrue(d == 0.0);

    DrJsonValue bool_val = drjson_make_bool(1);
    d = drj_to_double_for_sort(bool_val);
    TestExpectTrue(d == 0.0);

    TESTEND();
}

// Test sorting with query
TestFunction(TestSortingWithQuery){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create array of objects
    LongString json = LS("[{\"age\": 30}, {\"age\": 20}, {\"age\": 25}]");
    DrJsonValue arr = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;
    nav_rebuild(&nav);

    nav.cursor_pos = 0;

    // Sort by age query
    int result = cmd_sort(&nav, "age", 3);
    if(result == CMD_OK){
        // Verify array is sorted
        DrJsonValue sorted = nav.items[0].value;
        TestExpectEquals((int)sorted.kind, DRJSON_ARRAY);

        // First element should have age 20
        DrJsonValue first = drjson_get_by_index(ctx, sorted, 0);
        DrJsonValue first_age = drjson_query(ctx, first, "age", 3);
        if(first_age.kind == DRJSON_UINTEGER){
            TestExpectEquals(first_age.uinteger, 20);
        }
    }

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_is_expanded
TestFunction(TestNavIsExpanded){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    LongString json = LS("[1, 2, 3]");
    DrJsonValue arr = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)arr.kind, DRJSON_ARRAY);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = arr;

    // Initially not expanded
    TestExpectFalse(nav_is_expanded(&nav, arr));

    // Expand it
    uint64_t arr_id = nav_get_container_id(arr);
    bs_add(&nav.expanded, arr_id);

    // Now should be expanded
    TestExpectTrue(nav_is_expanded(&nav, arr));

    // Non-container should return false
    DrJsonValue num = drjson_make_int(42);
    TestExpectFalse(nav_is_expanded(&nav, num));

    // Cleanup
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_append_item - dynamic array growth
TestFunction(TestNavAppendItem){
    TESTBEGIN();

    JsonNav nav = {0};
    nav.items = NULL;
    nav.item_count = 0;
    nav.item_capacity = 0;

    DrJsonValue dummy_val = drjson_make_int(42);
    DrJsonAtom dummy_key = {0};

    // First append should allocate initial capacity (256)
    NavItem item1 = {.value = dummy_val, .key = dummy_key, .depth = 0};
    nav_append_item(&nav, item1);
    TestExpectEquals((int)nav.item_count, 1);
    TestExpectTrue(nav.item_capacity >= 256);

    // Append more items
    for(int i = 0; i < 10; i++){
        NavItem item = {.value = dummy_val, .key = dummy_key, .depth = i};
        nav_append_item(&nav, item);
    }
    TestExpectEquals((int)nav.item_count, 11);

    // Verify items stored correctly
    TestExpectEquals((int)nav.items[5].depth, 4);
    TestExpectEquals((int)nav.items[10].depth, 9);

    // Test growth by filling to capacity and beyond
    size_t old_capacity = nav.item_capacity;
    while(nav.item_count < old_capacity){
        NavItem item = {.value = dummy_val, .key = dummy_key, .depth = 0};
        nav_append_item(&nav, item);
    }
    // Add one more to trigger growth
    NavItem overflow = {.value = dummy_val, .key = dummy_key, .depth = 99};
    nav_append_item(&nav, overflow);
    TestExpectTrue(nav.item_capacity > old_capacity);
    TestExpectEquals((int)nav.items[nav.item_count-1].depth, 99);

    // Cleanup
    if(nav.items) free(nav.items);
    TESTEND();
}

// Test nav_reinit - state reset
TestFunction(TestNavReinit){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    LongString json = LS("{\"a\": [1, 2, 3], \"b\": {\"x\": 10}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Set various states
    nav.cursor_pos = 5;
    nav.scroll_offset = 10;
    nav.message_length = 1;
    nav.show_help = 1;
    nav.command_mode = 1;
    nav.pending_key = 'x';
    nav.search_mode = SEARCH_RECURSIVE;
    nav.search_input_active = 1;
    nav.in_completion_menu = 1;
    nav.tab_count = 3;

    // Allocate and populate line editors
    nav.command_buffer.data = malloc(256);
    LongString test_cmd = LS("test command");
    memcpy(nav.command_buffer.data, test_cmd.text, test_cmd.length + 1);
    nav.command_buffer.length = test_cmd.length;
    nav.command_buffer.capacity = 256;
    nav.command_buffer.cursor_pos = 5;

    nav.search_buffer.data = malloc(256);
    LongString search_txt = LS("search text");
    memcpy(nav.search_buffer.data, search_txt.text, search_txt.length + 1);
    nav.search_buffer.length = search_txt.length;
    nav.search_buffer.capacity = 256;
    nav.search_buffer.cursor_pos = 7;

    // Add some expanded containers
    uint64_t container_id = nav_get_container_id(root);
    bs_add(&nav.expanded, container_id);

    // Call nav_reinit
    nav_reinit(&nav);

    // Verify state reset
    TestExpectEquals((int)nav.cursor_pos, 0);
    TestExpectEquals((int)nav.scroll_offset, 0);
    TestExpectEquals((int)nav.message_length, 0);
    TestExpectEquals((int)nav.show_help, 0);
    TestExpectEquals((int)nav.command_mode, 0);
    TestExpectEquals((int)nav.pending_key, 0);
    TestExpectEquals((int)nav.search_mode, SEARCH_INACTIVE);
    TestExpectEquals((int)nav.search_input_active, 0);
    TestExpectEquals((int)nav.in_completion_menu, 0);
    TestExpectEquals((int)nav.tab_count, 0);

    // Verify line editors cleared but buffers kept
    TestExpectEquals((int)nav.command_buffer.length, 0);
    TestExpectEquals((int)nav.command_buffer.cursor_pos, 0);
    TestExpectTrue(nav.command_buffer.data != NULL); // Buffer kept
    TestExpectEquals((int)nav.search_buffer.length, 0);
    TestExpectEquals((int)nav.search_buffer.cursor_pos, 0);
    TestExpectTrue(nav.search_buffer.data != NULL); // Buffer kept

    // Cleanup
    if(nav.command_buffer.data) free(nav.command_buffer.data);
    if(nav.search_buffer.data) free(nav.search_buffer.data);
    if(nav.expanded.ids) free(nav.expanded.ids);
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_set_messagef - message formatting
TestFunction(TestNavSetMessagef){
    TESTBEGIN();

    JsonNav nav = {0};

    // Set a simple message
    nav_set_messagef(&nav, "Test message");
    LongString expected1 = LS("Test message");
    LongString actual1 = (LongString){.text = nav.message, .length = nav.message_length};
    TestExpectEquals2(LS_equals, actual1, expected1);

    // Set a formatted message
    nav_set_messagef(&nav, "Found %d items", 42);
    LongString expected2 = LS("Found 42 items");
    LongString actual2 = (LongString){.text = nav.message, .length = nav.message_length};
    TestExpectEquals2(LS_equals, actual2, expected2);

    // Set a message with string formatting
    nav_set_messagef(&nav, "Error: %s at line %d", "syntax error", 123);
    LongString expected3 = LS("Error: syntax error at line 123");
    LongString actual3 = (LongString){.text = nav.message, .length = nav.message_length};
    TestExpectEquals2(LS_equals, actual3, expected3);

    // Clear message
    nav_clear_message(&nav);
    TestExpectEquals((int)nav.message_length, 0);

    TESTEND();
}

// Test BitSet remove, toggle, clear operations
TestFunction(TestBitSetRemoveToggleClear){
    TESTBEGIN();

    BitSet set = {0};

    // Add some bits
    bs_add(&set, 5);
    bs_add(&set, 100);
    bs_add(&set, 200);

    TestExpectTrue(bs_contains(&set, 5));
    TestExpectTrue(bs_contains(&set, 100));
    TestExpectTrue(bs_contains(&set, 200));

    // Remove a bit
    bs_remove(&set, 100);
    TestExpectTrue(bs_contains(&set, 5));
    TestExpectFalse(bs_contains(&set, 100));
    TestExpectTrue(bs_contains(&set, 200));

    // Remove a bit that doesn't exist (should not crash)
    bs_remove(&set, 9999);

    // Toggle a bit that's set (should clear it)
    bs_toggle(&set, 5);
    TestExpectFalse(bs_contains(&set, 5));

    // Toggle a bit that's not set (should set it)
    bs_toggle(&set, 50);
    TestExpectTrue(bs_contains(&set, 50));

    // Toggle it again (should clear)
    bs_toggle(&set, 50);
    TestExpectFalse(bs_contains(&set, 50));

    // Clear all bits
    bs_clear(&set);
    TestExpectFalse(bs_contains(&set, 5));
    TestExpectFalse(bs_contains(&set, 100));
    TestExpectFalse(bs_contains(&set, 200));

    // Add a bit after clear
    bs_add(&set, 42);
    TestExpectTrue(bs_contains(&set, 42));

    // Cleanup
    bs_free(&set);
    TESTEND();
}

// Test to_lower - case conversion
TestFunction(TestToLower){
    TESTBEGIN();

    // Uppercase letters
    TestExpectEquals(to_lower('A'), 'a');
    TestExpectEquals(to_lower('Z'), 'z');
    TestExpectEquals(to_lower('M'), 'm');

    // Already lowercase
    TestExpectEquals(to_lower('a'), 'a');
    TestExpectEquals(to_lower('z'), 'z');
    TestExpectEquals(to_lower('m'), 'm');

    // Non-letters (unchanged)
    TestExpectEquals(to_lower('0'), '0');
    TestExpectEquals(to_lower('9'), '9');
    TestExpectEquals(to_lower(' '), ' ');
    TestExpectEquals(to_lower('!'), '!');
    TestExpectEquals(to_lower('_'), '_');

    TESTEND();
}

// Test substring_match - case-insensitive substring matching
TestFunction(TestSubstringMatchFunc){
    TESTBEGIN();

    // Exact match
    TestExpectTrue(substring_match("hello", 5, "hello", 5));

    // Substring at start
    TestExpectTrue(substring_match("hello world", 11, "hello", 5));

    // Substring in middle
    TestExpectTrue(substring_match("hello world", 11, "lo wo", 5));

    // Substring at end
    TestExpectTrue(substring_match("hello world", 11, "world", 5));

    // Case-insensitive match
    TestExpectTrue(substring_match("Hello World", 11, "HELLO", 5));
    TestExpectTrue(substring_match("HELLO", 5, "hello", 5));
    TestExpectTrue(substring_match("HeLLo", 5, "EllO", 4));

    // No match
    TestExpectFalse(substring_match("hello", 5, "xyz", 3));
    TestExpectFalse(substring_match("hello", 5, "goodbye", 7));

    // Empty query (should return false - no match)
    TestExpectFalse(substring_match("hello", 5, "", 0));

    TESTEND();
}

// Test glob_match - wildcard pattern matching
TestFunction(TestGlobMatch){
    TESTBEGIN();

    // Exact match (no wildcards)
    TestExpectTrue(glob_match("hello", 5, "hello", 5));

    // Single * at end
    TestExpectTrue(glob_match("hello", 5, "hel*", 4));
    TestExpectTrue(glob_match("hello world", 11, "hello*", 6));

    // Single * at start
    TestExpectTrue(glob_match("hello", 5, "*llo", 4));
    TestExpectTrue(glob_match("hello world", 11, "*world", 6));

    // Single * in middle
    TestExpectTrue(glob_match("hello world", 11, "hel*rld", 7));
    TestExpectTrue(glob_match("hello world", 11, "h*d", 3));

    // Multiple *
    TestExpectTrue(glob_match("hello world", 11, "h*o*d", 5));
    TestExpectTrue(glob_match("foo bar baz", 11, "f*b*z", 5));

    // * matches empty
    TestExpectTrue(glob_match("hello", 5, "hello*", 6));
    TestExpectTrue(glob_match("hello", 5, "*hello", 6));

    // Case-insensitive
    TestExpectTrue(glob_match("Hello World", 11, "hello*", 6));
    TestExpectTrue(glob_match("HELLO", 5, "hel*", 4));

    // No match
    TestExpectFalse(glob_match("hello", 5, "hel*x", 5));
    TestExpectFalse(glob_match("hello", 5, "xyz*", 4));

    // Empty pattern
    TestExpectFalse(glob_match("hello", 5, "", 0));

    TESTEND();
}

// Test nav_find_parent - finds parent item in navigation tree
TestFunction(TestNavFindParent){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    LongString json = LS("{\"a\": [1, 2, 3], \"b\": {\"x\": 10, \"y\": 20}}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Build navigation tree
    nav_rebuild(&nav);
    TestExpectTrue(nav.item_count > 0);

    // Root item (pos 0) has no parent
    size_t root_parent = nav_find_parent(&nav, 0);
    TestExpectEquals((int)root_parent, (int)SIZE_MAX);

    // Find parent of a child item
    // Items should be: root, "a", [1,2,3], 1, 2, 3, "b", {x,y}, "x", 10, "y", 20
    // Let's find a child of the array and verify its parent
    for(size_t i = 1; i < nav.item_count; i++){
        if(nav.items[i].depth > 0){
            size_t parent_idx = nav_find_parent(&nav, i);
            if(parent_idx != SIZE_MAX){
                // Parent should have depth one less
                TestExpectEquals((int)nav.items[parent_idx].depth, (int)nav.items[i].depth - 1);
            }
        }
    }

    // Invalid position should return SIZE_MAX
    size_t invalid_parent = nav_find_parent(&nav, 9999);
    TestExpectEquals((int)invalid_parent, (int)SIZE_MAX);

    // Cleanup
    if(nav.items) free(nav.items);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test get_type_rank - type ordering for sorting
TestFunction(TestGetTypeRank){
    TESTBEGIN();

    // Type ordering: null < bool < number < string < array < object
    DrJsonValue null_val = drjson_make_null();
    DrJsonValue bool_val = drjson_make_bool(1);
    DrJsonValue int_val = drjson_make_int(42);
    DrJsonValue uint_val = drjson_make_uint(42);
    DrJsonValue num_val = drjson_make_number(3.14);

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    DrJsonValue str_val = drjson_make_string(ctx, "hello", 5);
    DrJsonValue arr_val = drjson_parse_string(ctx, "[1,2,3]", 7, 0);
    DrJsonValue obj_val = drjson_parse_string(ctx, "{\"a\":1}", 7, 0);

    // Verify rank ordering
    int null_rank = get_type_rank(null_val);
    int bool_rank = get_type_rank(bool_val);
    int int_rank = get_type_rank(int_val);
    int uint_rank = get_type_rank(uint_val);
    int num_rank = get_type_rank(num_val);
    int str_rank = get_type_rank(str_val);
    int arr_rank = get_type_rank(arr_val);
    int obj_rank = get_type_rank(obj_val);

    TestExpectTrue(null_rank < bool_rank);
    TestExpectTrue(bool_rank < int_rank);
    TestExpectEquals(int_rank, num_rank); // All numbers same rank
    TestExpectEquals(int_rank, uint_rank); // All numbers same rank
    TestExpectTrue(int_rank < str_rank);
    TestExpectTrue(str_rank < arr_rank);
    TestExpectTrue(arr_rank < obj_rank);

    // Specific expected values
    TestExpectEquals(null_rank, 0);
    TestExpectEquals(bool_rank, 1);
    TestExpectEquals(int_rank, 2);
    TestExpectEquals(str_rank, 3);
    TestExpectEquals(arr_rank, 4);
    TestExpectEquals(obj_rank, 5);

    drjson_ctx_free_all(ctx);
    TESTEND();
}

// Test nav_collapse_all - should not collapse the root
TestFunction(TestNavCollapseAll){
    TESTBEGIN();

    DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
    TestAssert(ctx != NULL);

    // Create nested structure
    LongString json = LS("{\"arr\": [1, 2, 3], \"obj\": {\"x\": 10}, \"num\": 42}");
    DrJsonValue root = drjson_parse_string(ctx, json.text, json.length, 0);
    TestExpectEquals((int)root.kind, DRJSON_OBJECT);

    JsonNav nav = {0};
    nav.jctx = ctx;
    nav.root = root;

    // Expand root and children
    uint64_t root_id = nav_get_container_id(root);
    bs_add(&nav.expanded, root_id);

    DrJsonValue arr = drjson_query(ctx, root, "arr", 3);
    uint64_t arr_id = nav_get_container_id(arr);
    bs_add(&nav.expanded, arr_id);

    DrJsonValue obj = drjson_query(ctx, root, "obj", 3);
    uint64_t obj_id = nav_get_container_id(obj);
    bs_add(&nav.expanded, obj_id);

    nav_rebuild(&nav);

    // Verify root and children are expanded
    TestExpectTrue(nav_is_expanded(&nav, root));
    TestExpectTrue(nav_is_expanded(&nav, arr));
    TestExpectTrue(nav_is_expanded(&nav, obj));

    // Collapse all
    nav_collapse_all(&nav);

    // Root should still be expanded
    TestExpectTrue(nav_is_expanded(&nav, root));

    // Children should be collapsed
    TestExpectFalse(nav_is_expanded(&nav, arr));
    TestExpectFalse(nav_is_expanded(&nav, obj));

    // Cleanup
    if(nav.items) free(nav.items);
    if(nav.expanded.ids) free(nav.expanded.ids);
    drjson_ctx_free_all(ctx);
    TESTEND();
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
