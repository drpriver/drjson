#ifndef COMPILER_WARNINGS_H
#define COMPILER_WARNINGS_H

#if defined(__GNUC__) || defined(__clang__)
// #pragma GCC diagnostic error "-Wpedantic"
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Wvla"
#pragma GCC diagnostic error "-Wmissing-noreturn"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic error "-Wdeprecated"
#pragma GCC diagnostic error "-Wdouble-promotion"
#pragma GCC diagnostic error "-Wint-conversion"
#pragma GCC diagnostic error "-Wimplicit-int"
#pragma GCC diagnostic error "-Wimplicit-function-declaration"
#pragma GCC diagnostic error "-Wincompatible-pointer-types"
#pragma GCC diagnostic error "-Wunused-result"
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wformat"
#pragma GCC diagnostic error "-Wreturn-type"
#pragma GCC diagnostic ignored "-Woverlength-strings"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wnullability-extension"
#pragma clang diagnostic ignored "-Wfixed-enum-extension"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wgnu-auto-type"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic error "-Wassign-enum"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic error "-Warray-bounds-pointer-arithmetic"
#pragma clang diagnostic error "-Wcovered-switch-default"
#pragma clang diagnostic error "-Wfor-loop-analysis"
#pragma clang diagnostic error "-Winfinite-recursion"
#pragma clang diagnostic error "-Wduplicate-enum"
#pragma clang diagnostic error "-Wmissing-field-initializers"
#pragma clang diagnostic error "-Wpointer-type-mismatch"
#pragma clang diagnostic error "-Wextra-tokens"
#pragma clang diagnostic error "-Wmacro-redefined"
#pragma clang diagnostic error "-Winitializer-overrides"
#pragma clang diagnostic error "-Wsometimes-uninitialized"
#pragma clang diagnostic error "-Wunused-comparison"
#pragma clang diagnostic error "-Wundefined-internal"
#pragma clang diagnostic error "-Wnon-literal-null-conversion"
#pragma clang diagnostic ignored "-Wnullable-to-nonnull-conversion"
#pragma clang diagnostic error "-Wnullability-completeness"
#pragma clang diagnostic error "-Wnullability"
#pragma clang diagnostic error "-Wuninitialized"
#pragma clang diagnostic error "-Wconditional-uninitialized"
#pragma clang diagnostic error "-Wcomma"
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#endif

#endif
