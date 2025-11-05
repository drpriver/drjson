#include <stdio.h>
#include <string.h>
#define DRE_API
#include "dre.c"
#include "../testing.h"

#define TEST_MATCH(name, pat, txt, match, start, len) do { \
    DreContext ctx = {0}; \
    size_t match_start = 0; \
    int result = dre_match(&ctx, pat, sizeof(pat)-1, txt, sizeof(txt)-1, &match_start); \
    if(ctx.error != RE_ERROR_NONE) { \
        TestReport("Unexpected error in test: %s", name); \
        TestReport("  Error: %s", dre_error_name_table[ctx.error]); \
    } \
    TestExpectEquals(ctx.error, RE_ERROR_NONE); \
    TestExpectEquals(result, match); \
    if(match) { \
        TestExpectEquals(match_start, (size_t)start); \
        TestExpectEquals(ctx.match_length, (size_t)len); \
        if(match_start != (size_t)start || ctx.match_length != (size_t)len) { \
            TestReport("  Test: %s", name); \
            TestReport("  Pattern: '%s'  Text: '%s'", pat, txt); \
        } \
    } \
} while(0)

#define TEST_ERROR(name, pat, txt, err) do { \
    DreContext ctx = {0}; \
    size_t match_start = 0; \
    dre_match(&ctx, pat, sizeof(pat)-1, txt, sizeof(txt)-1, &match_start); \
    TestExpectEquals(ctx.error, err); \
    if(ctx.error != err) { \
        TestReport("  Test: %s", name); \
        TestReport("  Expected error: %s", dre_error_name_table[err]); \
        TestReport("  Got error: %s", ctx.error ? dre_error_name_table[ctx.error] : "NONE"); \
    } \
} while(0)

TestFunction(TestBasicLiterals) {
    TESTBEGIN();
    TEST_MATCH("Empty pattern", "", "", 1, 0, 0);
    TEST_MATCH("Empty text", "x", "", 0, 0, 0);
    TEST_MATCH("Single char match", "a", "a", 1, 0, 1);
    TEST_MATCH("Single char no match", "a", "b", 0, 0, 0);
    TEST_MATCH("Multi char match", "hello", "hello", 1, 0, 5);
    TEST_MATCH("Multi char in middle", "world", "hello world", 1, 6, 5);
    TEST_MATCH("Multi char at end", "end", "the end", 1, 4, 3);
    TEST_MATCH("Case sensitive", "ABC", "abc", 0, 0, 0);
    TESTEND();
}

TestFunction(TestDotWildcard) {
    TESTBEGIN();
    TEST_MATCH("Dot matches letter", "a.c", "abc", 1, 0, 3);
    TEST_MATCH("Dot matches digit", "a.c", "a9c", 1, 0, 3);
    TEST_MATCH("Dot matches space", "a.c", "a c", 1, 0, 3);
    TEST_MATCH("Dot doesn't match newline", "a.c", "a\nc", 0, 0, 0);
    TEST_MATCH("Multiple dots", "...", "xyz", 1, 0, 3);
    TEST_MATCH("Dot at start", ".bc", "abc", 1, 0, 3);
    TEST_MATCH("Dot at end", "ab.", "abc", 1, 0, 3);
    TESTEND();
}

TestFunction(TestAnchors) {
    TESTBEGIN();
    TEST_MATCH("Start anchor match", "^hello", "hello world", 1, 0, 5);
    TEST_MATCH("Start anchor no match", "^world", "hello world", 0, 0, 0);
    TEST_MATCH("End anchor match", "world$", "hello world", 1, 6, 5);
    TEST_MATCH("End anchor no match", "hello$", "hello world", 0, 0, 0);
    TEST_MATCH("Both anchors match", "^test$", "test", 1, 0, 4);
    TEST_MATCH("Both anchors no match", "^test$", "test!", 0, 0, 0);
    TEST_MATCH("Start anchor empty", "^", "anything", 1, 0, 0);
    TEST_MATCH("End anchor empty", "$", "", 1, 0, 0);
    TEST_MATCH("Start anchor only", "^", "", 1, 0, 0);
    TESTEND();
}

TestFunction(TestStarQuantifier) {
    TESTBEGIN();
    TEST_MATCH("Star zero matches", "a*b", "b", 1, 0, 1);
    TEST_MATCH("Star one match", "a*b", "ab", 1, 0, 2);
    TEST_MATCH("Star many matches", "a*b", "aaab", 1, 0, 4);
    TEST_MATCH("Star greedy", "a*", "aaaa", 1, 0, 4);
    TEST_MATCH("Star in middle", "x.*y", "x123y", 1, 0, 5);
    TEST_MATCH("Star multiple", "a*b*c", "aaabbbcc", 1, 0, 7);
    TEST_MATCH("Star dot combo", ".*", "anything", 1, 0, 8);
    TEST_MATCH("Star backtrack", "a*ab", "aaab", 1, 0, 4);
    TESTEND();
}

TestFunction(TestPlusQuantifier) {
    TESTBEGIN();
    TEST_MATCH("Plus requires one", "a+b", "b", 0, 0, 0);
    TEST_MATCH("Plus one match", "a+b", "ab", 1, 0, 2);
    TEST_MATCH("Plus many matches", "a+b", "aaab", 1, 0, 4);
    TEST_MATCH("Plus greedy", "a+", "aaaa", 1, 0, 4);
    TEST_MATCH("Plus in middle", "x.+y", "xy", 0, 0, 0);
    TEST_MATCH("Plus in middle match", "x.+y", "x1y", 1, 0, 3);
    TEST_MATCH("Plus backtrack", "a+ab", "aaab", 1, 0, 4);
    TESTEND();
}

TestFunction(TestQuestionQuantifier) {
    TESTBEGIN();
    TEST_MATCH("Question zero matches", "a?b", "b", 1, 0, 1);
    TEST_MATCH("Question one match", "a?b", "ab", 1, 0, 2);
    TEST_MATCH("Question greedy", "a?b", "aab", 1, 1, 2);
    TEST_MATCH("Question multiple", "a?b?c", "abc", 1, 0, 3);
    TEST_MATCH("Question multiple partial", "a?b?c", "ac", 1, 0, 2);
    TEST_MATCH("Question multiple none", "a?b?c", "c", 1, 0, 1);
    TEST_MATCH("Question truly optional", "ab?c", "ac", 1, 0, 2);
    TEST_MATCH("Question greedy at end", "ab?", "ab", 1, 0, 2);
    TEST_MATCH("Question needs char", "ab?c", "abc", 1, 0, 3);
    TESTEND();
}

TestFunction(TestCharacterClasses) {
    TESTBEGIN();
    TEST_MATCH("Class single char", "[a]", "a", 1, 0, 1);
    TEST_MATCH("Class multiple chars", "[abc]", "b", 1, 0, 1);
    TEST_MATCH("Class no match", "[abc]", "d", 0, 0, 0);
    TEST_MATCH("Class range lowercase", "[a-z]", "m", 1, 0, 1);
    TEST_MATCH("Class range uppercase", "[A-Z]", "M", 1, 0, 1);
    TEST_MATCH("Class range digits", "[0-9]", "5", 1, 0, 1);
    TEST_MATCH("Class multiple ranges", "[a-zA-Z]", "X", 1, 0, 1);
    TEST_MATCH("Class range and literal", "[a-z0]", "0", 1, 0, 1);
    TEST_MATCH("Class with quantifier", "[0-9]+", "123", 1, 0, 3);
    TEST_MATCH("Class inverted simple", "[^a]", "b", 1, 0, 1);
    TEST_MATCH("Class inverted no match", "[^a]", "a", 0, 0, 0);
    TEST_MATCH("Class inverted range", "[^0-9]", "x", 1, 0, 1);
    TESTEND();
}

TestFunction(TestEscapeSequences) {
    TESTBEGIN();
    TEST_MATCH("Escape digit", "\\d", "5", 1, 0, 1);
    TEST_MATCH("Escape digit no match", "\\d", "a", 0, 0, 0);
    TEST_MATCH("Escape digits multiple", "\\d+", "123", 1, 0, 3);
    TEST_MATCH("Escape non-digit", "\\D", "a", 1, 0, 1);
    TEST_MATCH("Escape non-digit no match", "\\D", "5", 0, 0, 0);
    TEST_MATCH("Escape word char", "\\w", "a", 1, 0, 1);
    TEST_MATCH("Escape word digit", "\\w", "5", 1, 0, 1);
    TEST_MATCH("Escape word underscore", "\\w", "_", 1, 0, 1);
    TEST_MATCH("Escape word no match", "\\w", " ", 0, 0, 0);
    TEST_MATCH("Escape non-word", "\\W", " ", 1, 0, 1);
    TEST_MATCH("Escape non-word no match", "\\W", "a", 0, 0, 0);
    TEST_MATCH("Escape space", "\\s", " ", 1, 0, 1);
    TEST_MATCH("Escape space tab", "\\s", "\t", 1, 0, 1);
    TEST_MATCH("Escape space newline", "\\s", "\n", 1, 0, 1);
    TEST_MATCH("Escape space no match", "\\s", "a", 0, 0, 0);
    TEST_MATCH("Escape non-space", "\\S", "a", 1, 0, 1);
    TEST_MATCH("Escape non-space no match", "\\S", " ", 0, 0, 0);
    TESTEND();
}

TestFunction(TestLiteralEscapes) {
    TESTBEGIN();
    TEST_MATCH("Escape backslash", "\\\\", "\\", 1, 0, 1);
    TEST_MATCH("Escape dot", "\\.", ".", 1, 0, 1);
    TEST_MATCH("Escape star", "\\*", "*", 1, 0, 1);
    TEST_MATCH("Escape plus", "\\+", "+", 1, 0, 1);
    TEST_MATCH("Escape question", "\\?", "?", 1, 0, 1);
    TEST_MATCH("Escape caret", "\\^", "^", 1, 0, 1);
    TEST_MATCH("Escape dollar", "\\$", "$", 1, 0, 1);
    TEST_MATCH("Escape bracket", "\\[", "[", 1, 0, 1);
    TEST_MATCH("Escape paren", "\\(", "(", 1, 0, 1);
    TEST_MATCH("Escape tab", "\\t", "\t", 1, 0, 1);
    TEST_MATCH("Escape newline", "\\n", "\n", 1, 0, 1);
    TESTEND();
}

TestFunction(TestComplexCombinations) {
    TESTBEGIN();
    TEST_MATCH("Anchor and quantifier", "^a+", "aaa", 1, 0, 3);
    TEST_MATCH("Anchor quantifier end", "a+$", "aaa", 1, 0, 3);
    TEST_MATCH("Class and quantifier", "[0-9]+\\.[0-9]+", "3.14", 1, 0, 4);
    TEST_MATCH("Multiple classes", "[a-z]+[0-9]+", "abc123", 1, 0, 6);
    TEST_MATCH("Escaped in class", "[\\d]+", "123", 1, 0, 3);
    TEST_MATCH("Complex email-like", "\\w+@\\w+\\.\\w+", "test@example.com", 1, 0, 16);
    TEST_MATCH("URL-like pattern", "\\w+://[a-z.]+", "http://test.com", 1, 0, 15);
    TEST_MATCH("Whitespace cleanup", "\\s+", "   ", 1, 0, 3);
    TEST_MATCH("Word boundaries sim", "\\w+", "hello world", 1, 0, 5);
    TESTEND();
}

TestFunction(TestGreedyBehavior) {
    TESTBEGIN();
    TEST_MATCH("Star greedy behavior", "a*a", "aaa", 1, 0, 3);
    TEST_MATCH("Plus greedy behavior", "a+a", "aaa", 1, 0, 3);
    TEST_MATCH("Dot star greedy", ".*x", "abcxyz", 1, 0, 4);
    TEST_MATCH("Multiple quantifiers", "a*b+c", "aaabbbcc", 1, 0, 7);
    TESTEND();
}

TestFunction(TestEdgeCases) {
    TESTBEGIN();
    TEST_MATCH("Match at position 0", "test", "test", 1, 0, 4);
    TEST_MATCH("Match at end", "end", "the end", 1, 4, 3);
    TEST_MATCH("No match anywhere", "xyz", "abc", 0, 0, 0);
    TEST_MATCH("Partial match fails", "abc", "ab", 0, 0, 0);
    TEST_MATCH("Pattern longer than text", "abcdef", "abc", 0, 0, 0);
    TEST_MATCH("Repeated pattern", "aba", "ababa", 1, 0, 3);
    TEST_MATCH("All quantifiers", "a*b+c?", "bbc", 1, 0, 3);
    TESTEND();
}

TestFunction(TestSpecialCharacters) {
    TESTBEGIN();
    TEST_MATCH("Hyphen in class end", "[a-]", "-", 1, 0, 1);
    TEST_MATCH("Right bracket literal", "\\]", "]", 1, 0, 1);
    TEST_MATCH("Multiple escapes", "\\d\\w\\s", "5a ", 1, 0, 3);
    TESTEND();
}

TestFunction(TestErrorCases) {
    TESTBEGIN();
    TEST_ERROR("Trailing backslash", "abc\\", "abc", RE_ERROR_ENDS_WITH_BACKSLASH);
    TEST_ERROR("Unclosed class", "[abc", "a", RE_ERROR_MISSING_RIGHT_SQUARE_BRACKET);
    TEST_ERROR("Unclosed class range", "[a-", "a", RE_ERROR_MISSING_RIGHT_SQUARE_BRACKET);
    TEST_ERROR("Branch not supported", "a|b", "a", RE_ERROR_BRANCH_NOT_IMPLEMENTED);
    TESTEND();
}

TestFunction(TestExtendedASCII) {
    TESTBEGIN();
    TEST_MATCH("Extended ASCII", "\\w", "\xC0", 0, 0, 0);
    TEST_MATCH("Extended ASCII digit", "\\d", "\xB2", 0, 0, 0);
    TESTEND();
}

TestFunction(TestQuantifierEdgeCases) {
    TESTBEGIN();
    // Quantifier at pattern start - no atom before it, should not match
    TEST_MATCH("Star at start", "*a", "a", 0, 0, 0);
    TEST_MATCH("Plus at start", "+a", "a", 0, 0, 0);
    TEST_MATCH("Question at start", "?a", "a", 0, 0, 0);
    // Multiple quantifiers in sequence - unusual but should handle gracefully
    TEST_MATCH("Double star", "a**", "aaa", 0, 0, 0);
    TEST_MATCH("Star on question", "a?*", "aa", 0, 0, 0);
    TESTEND();
}

TestFunction(TestAnchorEdgeCases) {
    TESTBEGIN();
    // Anchors matching empty string
    TEST_MATCH("Only anchors empty", "^$", "", 1, 0, 0);
    TEST_MATCH("Optional with anchors", "^a?$", "", 1, 0, 0);
    TEST_MATCH("Star with anchors", "^a*$", "", 1, 0, 0);
    TEST_MATCH("Plus with anchors empty", "^a+$", "", 0, 0, 0);
    // Anchors in middle of pattern become literals
    TEST_MATCH("Caret in middle literal", "ab^cd", "ab^cd", 1, 0, 5);
    TEST_MATCH("Dollar in middle literal", "ab$cd", "ab$cd", 1, 0, 5);
    TEST_MATCH("Double caret", "^^a", "^a", 1, 0, 2);
    TESTEND();
}

TestFunction(TestCharacterClassEdgeCases) {
    TESTBEGIN();
    // Hyphen in various positions
    TEST_MATCH("Hyphen only class", "[-]", "-", 1, 0, 1);
    TEST_MATCH("Hyphen at class start", "[-az]", "-", 1, 0, 1);
    TEST_MATCH("Hyphen at class start match a", "[-az]", "a", 1, 0, 1);
    TEST_MATCH("Hyphen at class end", "[az-]", "-", 1, 0, 1);
    // Special chars literal in class
    TEST_MATCH("Dot in class literal", "[.]", ".", 1, 0, 1);
    TEST_MATCH("Star in class literal", "[*]", "*", 1, 0, 1);
    TEST_MATCH("Plus in class literal", "[+]", "+", 1, 0, 1);
    TEST_MATCH("Question in class literal", "[?]", "?", 1, 0, 1);
    // Caret position matters
    TEST_MATCH("Caret not first in class", "[a^]", "^", 1, 0, 1);
    TEST_MATCH("Caret not first match a", "[a^]", "a", 1, 0, 1);
    // Empty class behavior
    TEST_MATCH("Empty class no match", "[]", "a", 0, 0, 0);
    TESTEND();
}

TestFunction(TestInvalidEscapes) {
    TESTBEGIN();
    // Invalid escape sequences should error
    TEST_ERROR("Bad escape x", "\\x", "x", RE_ERROR_BAD_ESCAPE);
    TEST_ERROR("Bad escape z", "\\z", "z", RE_ERROR_BAD_ESCAPE);
    TEST_ERROR("Bad escape k", "\\k", "k", RE_ERROR_BAD_ESCAPE);
    // Valid escapes for comparison
    TEST_MATCH("Valid escape d", "\\d", "5", 1, 0, 1);
    TEST_MATCH("Valid escape w", "\\w", "a", 1, 0, 1);
    TESTEND();
}

TestFunction(TestZeroWidthMatches) {
    TESTBEGIN();
    // Zero-width matches with quantifiers
    TEST_MATCH("Star on empty", "a*", "", 1, 0, 0);
    TEST_MATCH("Question on empty", "a?", "", 1, 0, 0);
    TEST_MATCH("Double star empty", "a*b*", "", 1, 0, 0);
    TEST_MATCH("Star zero then plus", "a*b+", "bbb", 1, 0, 3);
    TESTEND();
}

TestFunction(TestGreedyBacktrackingStress) {
    TESTBEGIN();
    // Patterns that require extensive backtracking
    TEST_MATCH("Multiple stars no match", "a*a*a*b", "aaaaaa", 0, 0, 0);
    TEST_MATCH("Multiple stars with match", "a*a*a*b", "aaaaab", 1, 0, 6);
    TEST_MATCH("Dot star twice", ".*.*x", "abcx", 1, 0, 4);
    TEST_MATCH("Deep backtrack success", "a*a*a*a*a*b", "aaaaab", 1, 0, 6);
    // Greedy consumption then backtrack
    TEST_MATCH("Greedy dots backtrack", ".*.*.*x", "x", 1, 0, 1);
    TESTEND();
}

int main(int argc, char **argv) {
    RegisterTest(TestBasicLiterals);
    RegisterTest(TestDotWildcard);
    RegisterTest(TestAnchors);
    RegisterTest(TestStarQuantifier);
    RegisterTest(TestPlusQuantifier);
    RegisterTest(TestQuestionQuantifier);
    RegisterTest(TestCharacterClasses);
    RegisterTest(TestEscapeSequences);
    RegisterTest(TestLiteralEscapes);
    RegisterTest(TestComplexCombinations);
    RegisterTest(TestGreedyBehavior);
    RegisterTest(TestEdgeCases);
    RegisterTest(TestSpecialCharacters);
    RegisterTest(TestErrorCases);
    RegisterTest(TestExtendedASCII);
    RegisterTest(TestQuantifierEdgeCases);
    RegisterTest(TestAnchorEdgeCases);
    RegisterTest(TestCharacterClassEdgeCases);
    RegisterTest(TestInvalidEscapes);
    RegisterTest(TestZeroWidthMatches);
    RegisterTest(TestGreedyBacktrackingStress);

    return test_main(argc, argv, NULL);
}
