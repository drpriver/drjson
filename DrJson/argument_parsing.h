//
// Copyright © 2021-2022, David Priver
//
#ifndef ARGUMENT_PARSING_H
#define ARGUMENT_PARSING_H
// size_t
#include <stddef.h>
// bool
#include <stdbool.h>
// integer types
#include <stdint.h>
// strtod, strtof
#include <stdlib.h>
// fprintf
#include <stdio.h>
#include <assert.h>
#include "long_string.h"
#include "parse_numbers.h"

#ifndef arrlen
#define arrlen(arr) (sizeof(arr)/sizeof((arr)[0]))
#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Nonnull
#define _Nonnull
#endif
#ifndef _Null_unspecified
#define _Null_unspecified
#endif
#endif

#ifndef unreachable
#if defined(__GNUC__) || defined(__clang__)
#define unreachable() __builtin_unreachable()
#else
#define unreachable() __assume(0)
#endif
#endif
//
// Parses argv (like from main) into variables.
// Supports parsing argv into strings, ints, unsigned ints (decimal, binary,
// hex), floats, doubles, flags, bitflags and user defined types.
//
// Supports fixed size arrays and a user defined add function.
//
enum ArgParseError {
    // parsing succeeded.
    ARGPARSE_NO_ERROR = 0,

    // Failed to convert a string into a value, like 'hello' can't convert to
    // an integer
    ARGPARSE_CONVERSION_ERROR = 1,

    // Given keyword arg-like parameter doesn't match any known args.
    ARGPARSE_UNKNOWN_KWARG = 2,

    // A keyword argument was given multiple times in the command line.
    ARGPARSE_DUPLICATE_KWARG = 3,

    // Greater than the maximum number of arguments were given for an arg.
    ARGPARSE_EXCESS_ARGS = 4,

    // Fewer than the minimum number of arguments were given for an arg.
    ARGPARSE_INSUFFICIENT_ARGS = 5,

    // Named at the commandline, but no arguments given. This is a user error
    // even if min_num is 0 as it can be very confusing otherwie.
    ARGPARSE_VISITED_NO_ARG_GIVEN = 6,

    // Something went wrong, but it is a logic error or a configuration error.
    ARGPARSE_INTERNAL_ERROR = 7,
};

enum ArgParseFlags {
    // Nothing
    ARGPARSE_FLAGS_NONE = 0,

    // Treat args looking like "-foo" that don't match a kwarg as strings to be
    // parsed as an argument rather than treated as an erroneous keyword.
    ARGPARSE_FLAGS_UNKNOWN_KWARGS_AS_ARGS = 1 << 0,

    // Skip 0 length strings.
    ARGPARSE_FLAGS_SKIP_EMPTY_STRINGS = 1 << 1,
    // Skip NULL strings.
    // This is useful if you do a pre-pass on the args and want to
    // remove arguments without having to shuffle the entire argv.
    ARGPARSE_FLAGS_SKIP_NULL_STRINGS = 1 << 2,
};

typedef struct Args Args;
struct Args {
    // argc/argv should exclude the program name, as it is useless
    int argc;
    const char*_Nonnull const *_Null_unspecified argv;
};
typedef struct ArgParser ArgParser;
//
// Parses the Args into the variables. Returns an error if there was any issue
// while parsing. Note that this function does not print anything if parsing
// failed.  If parsing failed, the destination variables could have some
// initialized and some not. Safe to assume they are all indeterminate.
//
// If parsing failed, the calling application should probably print the help
// and exit. This doesn't do that for you as libraries that call exit() are
// evil.
//
static inline
enum ArgParseError
parse_args(ArgParser* parser, const Args* args, enum ArgParseFlags);

//
// Like parse args, but takes pointer + length to an
// array of LongStrings.
static inline
enum ArgParseError
parse_args_longstrings(ArgParser* parser, const LongString* longstrings, size_t count, enum ArgParseFlags);

//
// After receiving a non-zero error code from `parse_args`, use this function
// to explain what failed to parse and why.
static inline
void
print_argparse_error(ArgParser* parser, enum ArgParseError error);

//
// Check for arguments like `-h` or `--version` that mean to ignore all other
// arguments and take an immediate action.
// This just returns the index of what matched, or -1 if there was no match.
// The caller should take the actual action and exit, return, etc. as
// appropriate. Parsing will almost surely fail.
static inline
intptr_t
check_for_early_out_args(ArgParser* parser, const Args* args);

//
// Like check_for_early_out_args, but with long strings
static inline
intptr_t
check_for_early_out_args_longstrings(ArgParser* parser, const LongString* args, size_t args_count);
//
// Prints a formatted help display for the command line arguments.
// Second argument is the wrap width.
//
static inline void print_argparse_help(const ArgParser*, int);

static inline void print_argparse_fish_completions(const ArgParser*);


//
// X-macro for the current kinds of args we can parse.
// Self explanatory, except ARG_UINTEGER64 accepts decimal (95), binary
// (0b1011111), and hex (0x5f) format.
//
// Format is apply(ArgType, type, "user string)
#define ARGS(apply) \
    apply(ARG_INTEGER64, int64_t, "int64") \
    apply(ARG_INT, int, "int") \
    apply(ARG_FLAG, bool, "flag") \
    apply(ARG_STRING, LongString, "string") \
    apply(ARG_CSTRING, const char*, "string") \
    apply(ARG_UINTEGER64, uint64_t, "uint64") \
    apply(ARG_FLOAT32, float, "float32") \
    apply(ARG_FLOAT64, double, "float64") \


typedef enum ArgType {
#define X(enumname, b, c) enumname,
    ARGS(X)
#undef X
    ARG_BITFLAG,
    ARG_ENUM,
    ARG_USER_DEFINED,
} ArgType;

static const LongString ArgTypeNames[] = {
#define X(a,b, string) LSINIT(string),
    ARGS(X)
#undef X
    LSINIT("flag"),
    LSINIT("enum"),
    LSINIT("USER DEFINED THIS IS A BUG"),
};

// Type Generic macro allows us to turn a type into an enum.
#define ARGTYPE(_x) _Generic(_x, \
        int64_t: ARG_INTEGER64, \
        uint64_t: ARG_UINTEGER64, \
        float: ARG_FLOAT32, \
        double: ARG_FLOAT64, \
        int: ARG_INT, \
        bool: ARG_FLAG, \
        const char*: ARG_CSTRING, \
        char*: ARG_CSTRING, \
        StringView: ARG_STRING, \
        LongString: ARG_STRING)
//
// A structure for allowing the parsing of user defined types.
// Fill out a struct with the given fields and use the
// ARG_USER_DEFINED type.
//
// Instead of using the ARGDEST macro, you fill out the dest field
// of ArgToParse yourself. For example:
//
//  ArgToParse pos_args[] = {
//     ...
//     [2] = {
//         .name = SV("mytype"),
//         .min_num = 0,
//         .max_num = 3,
//         .dest = {
//             .type = ARG_USER_DEFINED,
//             .user_pointer = &mytype_def,
//             .pointer = &myarg,
//         },
//     },
//     ...
//  };
//
typedef struct ArgParseUserDefinedType ArgParseUserDefinedType;
struct ArgParseUserDefinedType {
    // Converts the given string into the defined type by writing
    // into the pointer.
    // Return non-zero to indicate a conversion error.
    // First argument is the user_data pointer from this struct.
    int (* converter)(void*_Null_unspecified, const char*, size_t, void*);
    // Should do something like:
    //    printf(" = %d,%d,%d", x, y, z);
    // Used when printing the help.
    void(*_Nullable default_printer)(void*);
    // Used when printing the help.
    LongString type_name;
    size_t type_size;
    // If you need complicated state in your converter function,
    // you can store whatever you want here.
    void* _Null_unspecified user_data;
};

//
// A structure for converting strings into enums.  The enum must start at 0. It
// can have holes in the values, but you will have to fill them in with 0
// length strings.
//
// NOTE: We just do a linear search over the strings.  In theory this is very
//       bad, but in practice a typical parse line will need to match against a
//       given enum only once so any fancy algorithm would require a pre-pass
//       over all the data anyway.  We could be faster if we required enums to
//       be sorted or be pre-placed in a perfect hash table, but this harms
//       usability too much.  If you need to parse a lot of enums with weird
//       requirements, then just a create a user defined type instead of using
//       this.
//
// NOTE: You don't have to use a literal enum. You can define the "enum" at
//       runtime and actually just have it be an index into an array.
typedef struct ArgParseEnumType ArgParseEnumType;
struct ArgParseEnumType {
    // In order to support packed enums, specify the size of the enum here
    // instead of just assuming it's an int.  Only powers of two are supported
    // and it will be interpreted as an unsigned integer.
    size_t enum_size;
    // This should be the largest enum value + 1.
    size_t enum_count;
    // This should be a pointer to an array of `StringView`s that is
    // `enum_count` in length.
    // These will be used for both printing the help and for
    // parsing strings into the enum, so they should be in a
    // format that you would type in a command line.
    const StringView* enum_names;
};

typedef struct ArgParseDestination ArgParseDestination;
struct ArgParseDestination {
    // The type of what pointer points to.
    ArgType type;
    // Pointer to the first element.
    void* pointer;
    union {
        // This should be set if type == ARG_USER_DEFINED. It's a pointer
        // to a structure that defines how to convert a string to the
        // value, how to print, etc.
        // See the struct definition for more information.
        const ArgParseUserDefinedType*_Nullable user_pointer;
        // This should be set if type == ARG_ENUM. It's a pointer to a
        // structure that defines the value enum values, its size, etc.
        // See the struct definition for more information.
        const ArgParseEnumType*_Nullable enum_pointer;
        // For the ARG_BITFLAG type, this will be '|='ed into the destination.
        uint64_t bitflag;
    };
};


//
// Given a pointer to the storage for an argument, sets
// the correct type tag enum (ARG_INTEGER64 or whatever).
// Use this to initialize the dest member of ArgToParse.
// If the storage is an array, give the pointer to the first
// element of the array and set the max_num appropriately.
//
#define ARGDEST(_x) ((ArgParseDestination){.type = ARGTYPE((_x)[0]), .pointer=_x})

// For bit flags.
static inline
ArgParseDestination
ArgBitFlagDest(uint64_t* pointer, uint64_t flag){
    return (ArgParseDestination){
        .type = ARG_BITFLAG,
        .pointer = pointer,
        .bitflag = flag,
    };
}

// For enums.
static inline
ArgParseDestination
ArgEnumDest(void* pointer, const ArgParseEnumType* enu){
    return (ArgParseDestination){
        .type = ARG_ENUM,
        .pointer = pointer,
        .enum_pointer = enu,
    };
}

// For a user defined type.
static inline
ArgParseDestination
ArgUserDest(void* pointer, const ArgParseUserDefinedType* udt){
    return (ArgParseDestination){
        .type = ARG_USER_DEFINED,
        .pointer = pointer,
        .user_pointer = udt,
    };
}

//
// A structure describing an argument to be parsed.
// Create an array of these, one for positional args and another for
// keyword args. The order in the array for the positional args
// will be the order they need to be parsed in.
typedef struct ArgToParse ArgToParse;
struct ArgToParse {
    //
    // The name of the argument (include the "-" for keyword arguments).
    StringView name;
    //
    // An alternate name of the argument. Optional.
    // Allows to have a short and longer version of argument (name is "--help",
    // altname is "-h")
    StringView altname1;
    //
    // Mininum number of arguments for this arg. Fewer than this is an error.
    int min_num;
    //
    // Maximum number of arguments for this arg. More than this is an error.
    // Greater than 1 means the dest is a pointer to the first element
    // of an array.
    // Unbounded types should just set to a very large number. Argv isn't that
    // big anyway.
    int max_num;
    //
    // How many were actually parsed. Initialize to 0. You can check
    // this to see if the arg was actually set or not.
    int num_parsed;
    //
    // Whether or not this argument was given at the commandline. For variable
    // length positional arguments that allow 0 args, this distinguishes
    // between an empty list being given versus not being given at all, which
    // is used to return an error in that case.
    // This isn't very useful for users to look at.

    // Also, used internally to avoid allowing duplicate keywords at the
    // commandline.
    bool visited;

    //
    // Whether to show the default value in the help printout.
    bool show_default; // maybe we'll want a bitflags field with options instead.

    //
    // Whether to hide this flag from the help output.
    // Keyword argument only.
    bool hidden;

    //
    // The description of the argument. When printed, the helpstring will be
    // tokenized and adjacent whitespace will be merged into a single space.
    // Newlines are preserved, so don't hardwrap your helpstring.
    // The helptext will be appropriately soft-wrapped on word boundaries.
    const char* help;

    //
    // Use the ARGDEST macro to intialize this for basic types.
    // Some helper functions for other types are described. Or you can fill it
    // out yourself.
    ArgParseDestination dest;

    //
    // To support dynamically allocated collections, you can set this field
    // to a function pointer that appends the given argument. Return a non-zero
    // value to indicate an error.
    //
    // First argument is the dest.pointer. Second argument is a pointer to the
    // parsed value. For non-user-defined types this is a pointer to the
    // parsed version of the thing.
    //
    // For user-defined types, we don't call the converter function and instead
    // pass a pointer to the string view as the second argument.
    // This function should do any string to value conversion itself if
    // needed.
    //
    // NOTE: You still need to set the user_pointer of the ArgParseDestination
    //       as it is used to lookup the type name for printing the help.
    int (*_Nullable append_proc)(void*, const void*);
};

typedef struct ArgParseStyling ArgParseStyling;
struct ArgParseStyling {
    // Set to true to not style the output at all.
    bool plain;
    // Don't print a "------" style heading underneath a section heading.
    bool no_dashed_header_underline;
    // Will be used before the name of a section heading.
    const char*_Nullable pre_header;
    // Used after a section heading, before the ':'.
    // If NULL, will use the reset style ansi sequence.
    const char*_Nullable post_header;

    // This is like the *_header members, but for the name of the arg.
    const char*_Nullable pre_argname;
    const char*_Nullable post_argname;
    // This is like the *_header members, but for the type of the arg.
    const char*_Nullable pre_typename;
    const char*_Nullable post_typename;
    // In the default style, these do nothing. If you set them, they will be
    // printed before and after the beginning of the description of each
    // argument (the explanatory text below the arg name and its type).
    const char*_Nullable pre_description;
    const char*_Nullable post_description;
};

// This is the above, but collected into a struct to simplify
// passing to specific argument printers.
// These pointers are nonnull.
typedef struct ArgStyle ArgStyle;
struct ArgStyle {
    const char* pre_header;
    const char* post_header;
    const char* pre_argname;
    const char* post_argname;
    const char* pre_typename;
    const char* post_typename;
    const char* pre_description;
    const char* post_description;
};

//
// Parser structure.
struct ArgParser {
    //
    // The name of the program. Usually argv[0], but you can do whatever you
    // want.
    const char* name;
    //
    // A one-line description of the program.
    const char* description;
    //
    // The args that mean to early out and immediately take an action.
    // Generally, you should put in a help and version action.
    struct {
        ArgToParse* args;
        size_t count;
    } early_out;
    //
    // The positional arguments. Create an array of these. The order in the
    // array will be the order they need to be parsed in.
    struct {
        ArgToParse* args;
        size_t count;
    } positional;
    //
    // The keyword arguments. Create an array of these.
    // The order doesn't matter.
    struct {
        ArgToParse* args;
        size_t count;
    } keyword;
    // If an error occurred, these are set depending on what error occurred.
    // Exactly when they are set or not is an implementation detail, so use
    // `print_argparse_error` instead.
    struct {
        // If failure happened while an option was identified, this will be set
        // to that arg.
        ArgToParse*_Nullable arg_to_parse;
        // If failure happened on a specific argument, this will be set.
        const char*_Nullable arg;
    } failed;
    // This allows you to control the appearance of the help text.  Leave as
    // nulls to use the default styling. Set to empty string to disable that
    // particular style. Set plain to true to disable styling entirely.
    ArgParseStyling styling;
};

//
// Prints the help for a single argument.
static inline
void
print_arg_help(const ArgToParse*, int, const ArgStyle* style);


// Internal helper struct for text-wrapping.
typedef struct HelpState HelpState;
struct HelpState {
    int output_width;
    int lead;
    int remaining;
};

// Handle text-wrapping, printing a newline and indenting if necessary.
static inline
void
help_state_update(HelpState* hs, int n_to_print){
    if(hs->remaining - n_to_print < 0){
        // This is a string with 80 spaces in it.
        const char* SPACES = "                                                                                ";
        printf("\n%.*s", hs->lead, SPACES);
        hs->remaining = hs->output_width;
    }
    hs->remaining -= n_to_print;
}

static inline
void
print_wrapped_help(const char*_Nullable, int);

static inline
ArgStyle
determine_styling(const ArgParser* p){
    const char* pre_argname = "\033[1m";
    const char* post_argname = "\033[0m";
    const char* pre_typename = "\033[3m";
    const char* post_typename = "\033[0m";
    const char* pre_header = "\033[1m";
    const char* post_header = "\033[0m";
    const char* pre_description = "";
    const char* post_description = "";
    if(p->styling.plain){
        pre_argname = "";
        post_argname = "";
        pre_typename = "";
        post_typename = "";
        pre_header = "";
        post_header = "";
        pre_description = "";
        post_description = "";
    }
    else {
        #define SETIFSET(x) if(p->styling.x) x = p->styling.x
        SETIFSET(pre_argname);
        SETIFSET(post_argname);
        SETIFSET(pre_typename);
        SETIFSET(post_typename);
        SETIFSET(pre_header);
        SETIFSET(post_header);
        SETIFSET(pre_description);
        SETIFSET(post_description);
        #undef SETIFSET
    }
    ArgStyle style = {
        .pre_header       = pre_header,
        .post_header      = post_header,
        .pre_argname      = pre_argname,
        .post_argname     = post_argname,
        .pre_typename     = pre_typename,
        .post_typename    = post_typename,
        .pre_description  = pre_description,
        .post_description = post_description,
    };
    return style;
}

// See top of file.
static inline
void
print_argparse_help(const ArgParser* p, int columns){
    ArgStyle style = determine_styling(p);
    printf("%s: %s\n\n", p->name, p->description);
    const int printed = printf("usage: %s", p->name);
    HelpState hs = {
        .output_width = columns - printed,
        .lead = printed,
        .remaining = 0,
    };
    hs.remaining = hs.output_width;
    for(size_t i = 0; i < p->positional.count; i++){
        ArgToParse* arg = &p->positional.args[i];
        if(arg->max_num > 1){
            int to_print = 1 + arg->name.length + 4;
            help_state_update(&hs, to_print);
            printf(" %s ...", arg->name.text);
        }
        else {
            int to_print = 1 + arg->name.length;
            help_state_update(&hs, to_print);
            printf(" %s", arg->name.text);
        }
    }
    for(size_t i = 0; i < p->keyword.count; i++){
        ArgToParse* arg = &p->keyword.args[i];
        if(arg->hidden)
            continue;
        if(arg->dest.type == ARG_FLAG || arg->dest.type == ARG_BITFLAG){
            if(arg->altname1.length){
                int to_print = sizeof(" [%s | %s]") - 5 + arg->name.length + arg->altname1.length;
                help_state_update(&hs, to_print);
                printf(" [%s | %s]", arg->name.text, arg->altname1.text);
            }
            else{
                int to_print = sizeof(" [%s]") - 3 + arg->name.length;
                help_state_update(&hs, to_print);
                printf(" [%s]", arg->name.text);
            }
        }
        else {
            if(arg->altname1.text){
                LongString tn = ArgTypeNames[arg->dest.type];
                int to_print = sizeof(" [%s | %s <%s>%s]") - 9 + arg->name.length + arg->altname1.length + tn.length + (arg->max_num > 1?sizeof(" ...")-1: 0);
                help_state_update(&hs, to_print);
                printf(" [%s | %s <%s>%s]", arg->name.text, arg->altname1.text, tn.text, arg->max_num > 1?" ...":"");
            }
            else{
                LongString tn = ArgTypeNames[arg->dest.type];
                int to_print = sizeof(" [%s <%s>%s]") - 7 + arg->name.length + tn.length + (arg->max_num > 1?sizeof(" ...")-1:0);
                help_state_update(&hs, to_print);
                printf(" [%s <%s>%s]", arg->name.text, tn.text, arg->max_num>1?" ...":"");
            }
        }
    }
    putchar('\n');
    if(p->early_out.count){
        printf("\n%sEarly Out Arguments%s:\n", style.pre_header, style.post_header);
        if(!p->styling.no_dashed_header_underline){
            printf("--------------------\n");
        }
    }
    for(size_t i = 0; i < p->early_out.count; i++){
        ArgToParse* early = &p->early_out.args[i];
        if(early->hidden) continue;
        if(early->altname1.length){
            printf("%s%s%s, %s%s%s:\n", style.pre_argname, early->name.text, style.post_argname, style.pre_argname, early->altname1.text, style.post_argname);
        }
        else{
            printf("%s%s%s:\n", style.pre_argname, early->name.text, style.post_argname);
        }
        printf("%s", style.pre_description);
        print_wrapped_help(early->help, columns);
        printf("%s", style.post_description);
    }
    if(p->positional.count){
        printf("\n%sPositional Arguments%s:\n", style.pre_header, style.post_header);
        if(!p->styling.no_dashed_header_underline){
            printf("---------------------\n");
        }
        for(size_t i = 0; i < p->positional.count; i++){
            ArgToParse* arg = &p->positional.args[i];
            print_arg_help(arg, columns, &style);
        }
    }
    // It's possible for all keyword arguments to be hidden,
    // so only print the header until we hit a non-hidden argument.
    bool printed_keyword_header = false;
    for(size_t i = 0; i < p->keyword.count; i++){
        ArgToParse* arg = &p->keyword.args[i];
        if(arg->hidden)
            continue;
        if(!printed_keyword_header){
            printed_keyword_header = true;
            printf("\n%sKeyword Arguments%s:\n", style.pre_header, style.post_header);
            if(!p->styling.no_dashed_header_underline)
                printf("------------------\n");
        }
        print_arg_help(arg, columns, &style);
    }
}


static inline
void
print_argparse_hidden_help(const ArgParser* p, int columns){
    ArgStyle style = determine_styling(p);
    // There might be no hidden args. Only print the header if we are actually
    // going to print an arg.
    bool printed_an_arg = false;
    for(size_t i = 0; i < p->keyword.count; i++){
        ArgToParse* arg = &p->keyword.args[i];
        if(!arg->hidden) continue;
        if(!printed_an_arg){
            printed_an_arg = true;
            printf("%sHidden Arguments%s:\n", style.pre_header, style.post_header);
            if(!p->styling.no_dashed_header_underline)
                printf("-----------------\n");
        }
        print_arg_help(arg, columns, &style);
    }
}

static inline
void
print_enum_options(const ArgParseEnumType*_Nullable enu_, const ArgStyle* style){
    if(!enu_) return;
    // cast away nullability
    const ArgParseEnumType* enu = enu_;
    printf("\n    %sOptions%s:\n", style->pre_header, style->post_header);
    printf("    --------\n");
    for(size_t i = 0; i < enu->enum_count; i++){
        printf("    [%2zu] %s\n", i, enu->enum_names[i].text);
    }
}

// See top of file.
static inline
void
print_arg_help(const ArgToParse* arg, int columns, const ArgStyle* style){
    const char* help = arg->help;
    const char* name = arg->name.text;
    ArgType type = arg->dest.type;

    LongString typename;
    switch(type){
        case ARG_USER_DEFINED:
            typename = arg->dest.user_pointer->type_name;
            break;
        default:
            typename = ArgTypeNames[type];
            break;
    }
    printf("%s%s%s", style->pre_argname, name, style->post_argname);
    if(arg->altname1.length){
        printf(", %s%s%s", style->pre_argname, arg->altname1.text, style->post_argname);
    }
    if(type == ARG_FLAG || type == ARG_BITFLAG)
        ;
    else{
        printf(" <%s%s%s>", style->pre_typename, typename.text, style->post_typename);
        if(arg->max_num > 1){
            printf(" ... ");
        }
    }


    if(arg->min_num != 0 || !arg->show_default){
        putchar('\n');
        printf("%s", style->pre_description);
        print_wrapped_help(help, columns);
        printf("%s", style->post_description);
        if(type == ARG_ENUM)
            print_enum_options(arg->dest.enum_pointer, style);
        return;
    }
    switch(type){
        case ARG_INTEGER64:{
            int64_t* data = arg->dest.pointer;
            printf(" = %lld", (long long)*data);
        }break;
        case ARG_UINTEGER64:{
            uint64_t* data = arg->dest.pointer;
            printf(" = %llu", (unsigned long long)*data);
        }break;
        case ARG_INT:{
            int* data = arg->dest.pointer;
            printf(" = %d", *data);
        }break;
        case ARG_FLOAT32:{
            float* data = arg->dest.pointer;
            printf(" = %f", (double)*data);
        }break;
        case ARG_FLOAT64:{
            double* data = arg->dest.pointer;
            printf(" = %f", *data);
        }break;
        case ARG_BITFLAG:{
        }break;
        case ARG_FLAG:{
        }break;
        case ARG_CSTRING:{
            const char* s = arg->dest.pointer;
            printf(" = '%s'", s);
        }break;
        case ARG_STRING:{
            LongString* s = arg->dest.pointer;
            printf(" = '%.*s'", (int)s->length, s->text);
        }break;
        case ARG_USER_DEFINED:{
            if(arg->dest.user_pointer->default_printer){
                arg->dest.user_pointer->default_printer(arg->dest.pointer);
            }
        }break;
        case ARG_ENUM:{
            const ArgParseEnumType* enu = arg->dest.enum_pointer;
            StringView enu_name = SV("???");
            switch(enu->enum_size){
                case 1:{
                    uint8_t* def = arg->dest.pointer;
                    if(*def <  enu->enum_count)
                        enu_name = enu->enum_names[*def];
                }break;
                case 2:{
                    uint16_t* def = arg->dest.pointer;
                    if(*def <  enu->enum_count)
                        enu_name = enu->enum_names[*def];
                }break;
                case 4:{
                    uint32_t* def = arg->dest.pointer;
                    if(*def <  enu->enum_count)
                        enu_name = enu->enum_names[*def];
                }break;
                case 8:{
                    uint64_t* def = arg->dest.pointer;
                    if(*def <  enu->enum_count)
                        enu_name = enu->enum_names[*def];
                }break;
            }
            printf(" = %.*s", (int)enu_name.length, enu_name.text);
            print_enum_options(enu, style);
        }break;
    }
    putchar('\n');
    printf("%s", style->pre_description);
    print_wrapped_help(help, columns);
    printf("%s", style->post_description);
    if(type == ARG_ENUM){
        const ArgParseEnumType* enu = arg->dest.enum_pointer;
        print_enum_options(enu, style);
    }
}

struct HelpTokenized {
    StringView token;
    bool is_newline;
    const char* rest;
};

// Tokenizes the string on whitespace.
// Internal helper for printing the help wrapped.
static inline
struct HelpTokenized
next_tokenize_help(const char* help){
    for(;;help++){
        switch(*help){
            case ' ': case '\r': case '\t': case '\f':
                continue;
            default:
                break;
        }
        break;
    }
    if(*help == '\n'){
        return (struct HelpTokenized){
            .is_newline = true,
            .rest = help+1,
        };
    }
    const char* begin = help;
    for(;;help++){
        switch(*help){
            // Note that this list includes '\0' as a word boundary.
            case ' ': case '\n': case '\r': case '\t': case '\f': case '\0':{
                return (struct HelpTokenized){
                    .token.text = begin,
                    .token.length = (size_t)(help - begin),
                    .rest = help,
                };
            }break;
            default:
                continue;
        }
    }
    unreachable();
}

static inline
void
print_wrapped(const char*text, int columns){
    HelpState hs = {.output_width = columns, .lead=0, .remaining=0};
    hs.remaining = hs.output_width;
    // Track if we had a hardbreak so we can preserve paragraph breaks.
    bool newline = false;
    for(;*text;){
        struct HelpTokenized tok = next_tokenize_help((const char*)text); // cast away nullability
        text = tok.rest;
        if(tok.is_newline){
            if(newline || hs.remaining != hs.output_width){
                putchar('\n');
                hs.remaining = hs.output_width;
            }
            newline = true;
            continue;
        }
        else {
            newline = false;
        }
        help_state_update(&hs, tok.token.length);
        printf("%.*s", (int)tok.token.length, tok.token.text);
        if(hs.remaining){
            putchar(' ');
            hs.remaining--;
        }
    }
    putchar('\n');
}

static inline
void
print_wrapped_help(const char*_Nullable help, int columns){
    if(!help){
        return;
    }
    printf("    ");
    HelpState hs = {.output_width = columns - 4, .lead = 4, .remaining = 0};
    hs.remaining = hs.output_width;
    for(;*help;){
        struct HelpTokenized tok = next_tokenize_help((const char*)help); // cast away nullability
        help = tok.rest;
        if(tok.is_newline){
            if(hs.remaining != hs.output_width){
                printf("\n    ");
                hs.remaining = hs.output_width;
            }
            continue;
        }
        help_state_update(&hs, tok.token.length);
        printf("%.*s", (int)tok.token.length, tok.token.text);
        if(hs.remaining){
            putchar(' ');
            hs.remaining--;
        }
    }
    putchar('\n');
}

static inline int agp_maxnum(int x){ return x?x:1; }

static inline enum ArgParseError set_flag(ArgToParse* arg);
// Parse a single argument from a string.
// Used internally. I guess you could use it if you really wanted to, but you
// don't need this type generic version?
static inline
enum ArgParseError
parse_arg(ArgToParse* arg, StringView s){
    // Append_procs should signal their own error.
    if(arg->num_parsed >= agp_maxnum(arg->max_num))
        return ARGPARSE_EXCESS_ARGS;
    // If previous num parsed is nonzero, this means
    // that what we are pointing to is an array.
#define APPEND_ARG(type, value_) do { \
    if(arg->append_proc){ \
        int fail = arg->append_proc(arg->dest.pointer, &value_); \
        if(fail) {\
            return ARGPARSE_CONVERSION_ERROR; \
        } \
        arg->num_parsed += 1; \
    } \
    else { \
        type* dest = arg->dest.pointer; \
        dest += arg->num_parsed; \
        *dest = value_; \
        arg->num_parsed += 1; \
    } \
}while(0)
    switch(arg->dest.type){
        case ARG_INTEGER64:{
            struct Int64Result e = parse_int64(s.text, s.length);
            if(e.errored){
                return ARGPARSE_CONVERSION_ERROR;
            }
            int64_t value = e.result;
            APPEND_ARG(int64_t, value);
        }break;
        case ARG_UINTEGER64:{
            struct Uint64Result e = parse_unsigned_human(s.text, s.length);
            if(e.errored) {
                return ARGPARSE_CONVERSION_ERROR;
            }
            uint64_t value = e.result;
            APPEND_ARG(uint64_t, value);
        }break;
        case ARG_INT:{
            struct IntResult e = parse_int(s.text, s.length);
            if(e.errored) {
                return ARGPARSE_CONVERSION_ERROR;
            }
            int value = e.result;
            APPEND_ARG(int, value);
        }break;
        case ARG_FLOAT32:{
            char* endptr;
            // I'd rather roll my own strtof, but that's too much work right now.
            float value = strtof(s.text, &endptr);
            if(endptr == s.text)
                return ARGPARSE_CONVERSION_ERROR;
            if(*endptr != '\0')
                return ARGPARSE_CONVERSION_ERROR;
            APPEND_ARG(float, value);
        }break;
        case ARG_FLOAT64:{
            char* endptr;
            // Ditto on rolling my own.
            double value = strtod(s.text, &endptr);
            if(endptr == s.text)
                return ARGPARSE_CONVERSION_ERROR;
            if(*endptr != '\0')
                return ARGPARSE_CONVERSION_ERROR;
            APPEND_ARG(double, value);
        }break;
        // for flags, using the append_proc doesn't make sense.
        case ARG_BITFLAG:
            // fall-through
        case ARG_FLAG:
            // This is weird, but it is a configuration error.
            return set_flag(arg);
        case ARG_STRING:{
            // This is a hack, our target is actually a LongString.
            // But it can also be a StringView and they pun to each other.
            APPEND_ARG(StringView, s);
        }break;
        case ARG_CSTRING:{
            APPEND_ARG(const char*, s.text);
        }break;
        case ARG_USER_DEFINED:{
            // This is error prone, but seemed like the best option.
            // We could alloca onto our stack, but that could be significantly
            // inefficient compare to forcing them to parse in their append proc.
            if(arg->append_proc){
                int e = arg->append_proc(arg->dest.pointer, &s);
                if(e) return ARGPARSE_CONVERSION_ERROR;
            }
            else {
                char* dest = arg->dest.pointer;
                dest += arg->dest.user_pointer->type_size * (size_t)arg->num_parsed;
                int e = arg->dest.user_pointer->converter(arg->dest.user_pointer->user_data, s.text, s.length, dest);
                if(e) return ARGPARSE_CONVERSION_ERROR;
            }
            arg->num_parsed += 1;
        }break;
        case ARG_ENUM:{
            if(!s.length) return ARGPARSE_CONVERSION_ERROR;
            const ArgParseEnumType* enu = arg->dest.enum_pointer;
            // allow specifying enums by numeric value.
            struct Uint64Result uint_res = parse_unsigned_human(s.text, s.length);
            if(!uint_res.errored){
                if(uint_res.result >= enu->enum_count)
                    return ARGPARSE_CONVERSION_ERROR;
                switch(enu->enum_size){
                    case 1:{
                        APPEND_ARG(uint8_t, uint_res.result);
                    }return 0;
                    case 2:{
                        APPEND_ARG(uint16_t, uint_res.result);
                    }return 0;
                    case 4:{
                        APPEND_ARG(uint32_t, uint_res.result);
                    }return 0;
                    case 8:{
                        APPEND_ARG(uint64_t, uint_res.result);
                    }return 0;
                    default:
                        return ARGPARSE_INTERNAL_ERROR;
                }
            }
            // We just do a linear search over the strings.  In theory this is
            // very bad, but in practice a typical parse line will need to
            // match against a given enum once so any fancy algorithm would
            // require a pre-pass over all the data anyway.  We could be faster
            // if we required enums to be sorted, but this harms usability too
            // much.  If you need to parse a lot of enums with weird
            // requirements, then just a create a user defined type instead of
            // using this.
            for(size_t i = 0; i < enu->enum_count; i++){
                if(SV_equals(enu->enum_names[i], s)){
                    switch(enu->enum_size){
                        case 1:{
                            APPEND_ARG(uint8_t, i);
                        }return 0;
                        case 2:{
                            APPEND_ARG(uint16_t, i);
                        }return 0;
                        case 4:{
                            APPEND_ARG(uint32_t, i);
                        }return 0;
                        case 8:{
                            APPEND_ARG(uint64_t, i);
                        }return 0;
                        default:
                            return ARGPARSE_INTERNAL_ERROR;
                    }
                }
            }
            return ARGPARSE_CONVERSION_ERROR;
        }break;
    }
    return 0;
}
#undef APPEND_ARG

// Set a flag. I really don't see why you would use this outside of this.
static inline
enum ArgParseError
set_flag(ArgToParse* arg){
    if(arg->dest.type == ARG_BITFLAG){
        uint64_t* dest = arg->dest.pointer;
        if(*dest & arg->dest.bitflag)
            return ARGPARSE_DUPLICATE_KWARG;
        *dest |= arg->dest.bitflag;
        arg->num_parsed += 1;
        return 0;
    }
    assert(arg->dest.type == ARG_FLAG);
    if(arg->num_parsed >= agp_maxnum(arg->max_num))
        return ARGPARSE_DUPLICATE_KWARG;
    bool* dest = arg->dest.pointer;
    *dest = true;
    arg->num_parsed += 1;
    return 0;
}

static inline
intptr_t
check_for_early_out_args(ArgParser* parser, const Args* args){
    for(int i = 0; i < args->argc; i++){
        StringView argstring = cstr_to_SV(args->argv[i]);
        for(size_t j = 0; j < parser->early_out.count; j++){
            ArgToParse* early = &parser->early_out.args[j];
            if(SV_equals(argstring, early->name))
                return (intptr_t)j;
            if(early->altname1.length && SV_equals(argstring, early->altname1))
                return (intptr_t)j;
        }
    }
    return -1;
}

static inline
intptr_t
check_for_early_out_args_longstrings(ArgParser* parser, const LongString* args, size_t args_count){
    for(size_t i = 0; i < args_count; i++){
        StringView argstring = LS_to_SV(args[i]);
        for(size_t j = 0; j < parser->early_out.count; j++){
            ArgToParse* early = &parser->early_out.args[j];
            if(SV_equals(argstring, early->name))
                return (intptr_t)j;
            if(early->altname1.length && SV_equals(argstring, early->altname1))
                return (intptr_t)j;
        }
    }
    return -1;
}


static inline
ArgToParse*_Nullable
find_matching_kwarg(ArgParser* parser, StringView sv){
    // do an inefficient linear search for now.
    for(size_t i = 0; i < parser->keyword.count; i++){
        ArgToParse* kw = &parser->keyword.args[i];
        if(SV_equals(kw->name, sv))
            return kw;
        if(kw->altname1.length){
            if(SV_equals(kw->altname1, sv))
                return kw;
        }
    }
    return NULL;
}

// See top of file.
static inline
enum ArgParseError
parse_args(ArgParser* parser, const Args* args, enum ArgParseFlags flags){
    ArgToParse* pos_arg = NULL;
    ArgToParse* past_the_end = NULL;
    if(parser->positional.count){
        pos_arg = &parser->positional.args[0];
        past_the_end = pos_arg + parser->positional.count;
    }
    ArgToParse* kwarg = NULL;
    const char*const* argv_end = args->argv?(args->argv+args->argc):NULL;
    for(const char*const* arg = args->argv; arg != argv_end; ++arg){
        if(!arg && (flags & ARGPARSE_FLAGS_SKIP_NULL_STRINGS))
            continue;
        if(!arg)
            return ARGPARSE_INTERNAL_ERROR;
        StringView s = cstr_to_SV(*arg);
        if(!s.length && (flags & ARGPARSE_FLAGS_SKIP_EMPTY_STRINGS))
            continue;
        if(s.length > 1){
            if(s.text[0] == '-'){
                bool number = s.text[1] == '.' || (s.text[1] >= '0' && s.text[1] <= '9');
                if(!number){
                    // Not a number, find matching kwarg
                    ArgToParse* new_kwarg = find_matching_kwarg(parser, s);
                    if(!new_kwarg){
                        if(flags & ARGPARSE_FLAGS_UNKNOWN_KWARGS_AS_ARGS)
                            break;
                        parser->failed.arg = *arg;
                        return ARGPARSE_UNKNOWN_KWARG;
                    }
                    if(new_kwarg->visited){
                        parser->failed.arg_to_parse = new_kwarg;
                        parser->failed.arg = *arg;
                        return ARGPARSE_DUPLICATE_KWARG;
                    }
                    if(pos_arg && pos_arg != past_the_end && pos_arg->visited)
                        pos_arg++;
                    kwarg = new_kwarg;
                    kwarg->visited = true;
                    if(kwarg->dest.type == ARG_FLAG || kwarg->dest.type == ARG_BITFLAG){
                        enum ArgParseError error = set_flag(kwarg);
                        if(error) {
                            parser->failed.arg_to_parse = kwarg;
                            parser->failed.arg = *arg;
                            return error;
                        }
                        kwarg = NULL;
                    }
                    continue;
                }
            }
        }
        if(kwarg){
            enum ArgParseError err = parse_arg(kwarg, s);
            if(err){
                parser->failed.arg = *arg;
                parser->failed.arg_to_parse = kwarg;
                return err;
            }
            if(kwarg->num_parsed == agp_maxnum(kwarg->max_num))
                kwarg = NULL;
        }
        else if(pos_arg && pos_arg != past_the_end){
            pos_arg->visited = true;
            enum ArgParseError err = parse_arg(pos_arg, s);
            if(err){
                parser->failed.arg = *arg;
                parser->failed.arg_to_parse = pos_arg;
                return err;
            }
            if(pos_arg->num_parsed == agp_maxnum(pos_arg->max_num))
                pos_arg++;
        }
        else {
            parser->failed.arg = *arg;
            return ARGPARSE_EXCESS_ARGS;
        }
    }
    for(size_t i = 0; i < parser->positional.count; i++){
        ArgToParse* arg = &parser->positional.args[i];
        if(arg->num_parsed < arg->min_num){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_INSUFFICIENT_ARGS;
        }
        if(arg->num_parsed > agp_maxnum(arg->max_num)){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_EXCESS_ARGS;
        }
    }
    for(size_t i = 0; i < parser->keyword.count; i++){
        ArgToParse* arg = &parser->keyword.args[i];
        if(arg->num_parsed < arg->min_num){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_INSUFFICIENT_ARGS;
        }
        if(arg->num_parsed > agp_maxnum(arg->max_num)){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_EXCESS_ARGS;
        }
        // This only makes sense for keyword arguments.
        if(arg->visited && arg->num_parsed == 0){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_VISITED_NO_ARG_GIVEN;
        }
    }
    return 0;
}

static inline
enum ArgParseError
parse_args_longstrings(ArgParser* parser, const LongString*args, size_t args_count, enum ArgParseFlags flags){
    ArgToParse* pos_arg = NULL;
    ArgToParse* past_the_end = NULL;
    if(parser->positional.count){
        pos_arg = &parser->positional.args[0];
        past_the_end = pos_arg + parser->positional.count;
    }
    ArgToParse* kwarg = NULL;
    for(size_t i = 0; i < args_count; i++){
        const LongString* arg = args+i;
        StringView s = LS_to_SV(*arg);
        if(!s.length && (flags & ARGPARSE_FLAGS_SKIP_EMPTY_STRINGS))
            continue;
        if(s.length > 1){
            if(s.text[0] == '-'){
                bool number = s.text[1] == '.' || (s.text[1] >= '0' && s.text[1] <= '9');
                if(!number){
                    // Not a number, find matching kwarg
                    ArgToParse* new_kwarg = find_matching_kwarg(parser, s);
                    if(!new_kwarg){
                        if(flags & ARGPARSE_FLAGS_UNKNOWN_KWARGS_AS_ARGS)
                            break;
                        // @Sus
                        parser->failed.arg = arg->text;
                        return ARGPARSE_UNKNOWN_KWARG;
                    }
                    if(new_kwarg->visited){
                        parser->failed.arg_to_parse = new_kwarg;
                        // @Sus
                        parser->failed.arg = arg->text;
                        return ARGPARSE_DUPLICATE_KWARG;
                    }
                    if(pos_arg && pos_arg != past_the_end && pos_arg->visited)
                        pos_arg++;
                    kwarg = new_kwarg;
                    kwarg->visited = true;
                    if(kwarg->dest.type == ARG_FLAG || kwarg->dest.type == ARG_BITFLAG){
                        enum ArgParseError error = set_flag(kwarg);
                        if(error) {
                            parser->failed.arg_to_parse = kwarg;
                            parser->failed.arg = arg->text;
                            return error;
                        }
                        kwarg = NULL;
                    }
                    continue;
                }
            }
        }
        if(kwarg){
            enum ArgParseError err = parse_arg(kwarg, s);
            if(err){
                parser->failed.arg = arg->text;
                parser->failed.arg_to_parse = kwarg;
                return err;
            }
            if(kwarg->num_parsed == agp_maxnum(kwarg->max_num))
                kwarg = NULL;
        }
        else if(pos_arg && pos_arg != past_the_end){
            pos_arg->visited = true;
            enum ArgParseError err = parse_arg(pos_arg, s);
            if(err){
                parser->failed.arg = arg->text;
                parser->failed.arg_to_parse = pos_arg;
                return err;
            }
            if(pos_arg->num_parsed == agp_maxnum(pos_arg->max_num))
                pos_arg++;
        }
        else {
            parser->failed.arg = arg->text;
            return ARGPARSE_EXCESS_ARGS;
        }
    }
    for(size_t i = 0; i < parser->positional.count; i++){
        ArgToParse* arg = &parser->positional.args[i];
        if(arg->num_parsed < arg->min_num){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_INSUFFICIENT_ARGS;
        }
        if(arg->num_parsed > agp_maxnum(arg->max_num)){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_EXCESS_ARGS;
        }
    }
    for(size_t i = 0; i < parser->keyword.count; i++){
        ArgToParse* arg = &parser->keyword.args[i];
        if(arg->num_parsed < arg->min_num){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_INSUFFICIENT_ARGS;
        }
        if(arg->num_parsed > agp_maxnum(arg->max_num)){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_EXCESS_ARGS;
        }
        // This only makes sense for keyword arguments.
        if(arg->visited && arg->num_parsed == 0){
            parser->failed.arg_to_parse = arg;
            return ARGPARSE_VISITED_NO_ARG_GIVEN;
        }
    }
    return 0;
}

static inline
void
print_argparse_error(ArgParser* parser, enum ArgParseError error){
    if(parser->failed.arg_to_parse){
        ArgToParse* arg_to_parse = parser->failed.arg_to_parse;
        fprintf(stderr, "Error when parsing argument for '%s': ", arg_to_parse->name.text);
    }
    switch(error){
        case ARGPARSE_NO_ERROR:
            break;
        case ARGPARSE_CONVERSION_ERROR:
            if(parser->failed.arg_to_parse){
                ArgToParse* arg_to_parse = parser->failed.arg_to_parse;
                if(parser->failed.arg){
                    const char* arg = parser->failed.arg;
                    switch(arg_to_parse->dest.type){
                        case ARG_INTEGER64:
                            fprintf(stderr, "Unable to parse an int64 from '%s'\n", arg);
                            return;
                        case ARG_INT:
                            fprintf(stderr, "Unable to parse an int from '%s'\n", arg);
                            return;
                            // These seem bizarre.
                        case ARG_STRING:
                            // fall-through
                        case ARG_CSTRING:
                            fprintf(stderr, "Unable to parse a string from '%s'\n", arg);
                            return;
                        case ARG_UINTEGER64:
                            fprintf(stderr, "Unable to parse a uint64 from '%s'\n", arg);
                            return;
                        case ARG_FLOAT32:
                            fprintf(stderr, "Unable to parse a float32 from '%s'\n", arg);
                            return;
                        case ARG_FLOAT64:
                            fprintf(stderr, "Unable to parse a float64 from '%s'\n", arg);
                            return;
                        case ARG_USER_DEFINED:
                            fprintf(stderr, "Unable to parse a %s from '%s'\n", arg_to_parse->dest.user_pointer->type_name.text, arg);
                            return;
                        case ARG_ENUM:
                            fprintf(stderr, "Unable to parse a choice from '%s'. Not a valid option.\n", arg);
                            return;
                        case ARG_BITFLAG:
                            // fall-through
                        case ARG_FLAG:
                            fprintf(stderr, "Unable to parse a flag. This is a bug.\n");
                            return;
                    }
                    fprintf(stderr, "Unable to parse an unknown type from '%s'\n", arg);
                    return;
                }
                else {
                    switch(arg_to_parse->dest.type){
                        case ARG_INTEGER64:
                            fprintf(stderr, "Unable to parse an int64 from unknown argument'\n");
                            return;
                        case ARG_INT:
                            fprintf(stderr, "Unable to parse an int from unknown argument'\n");
                            return;
                            // These seem bizarre.
                        case ARG_STRING:
                            // fall-through
                        case ARG_CSTRING:
                            fprintf(stderr, "Unable to parse a string from unknown argument.\n");
                            return;
                        case ARG_UINTEGER64:
                            fprintf(stderr, "Unable to parse a uint64 from unknown argument.\n");
                            return;
                        case ARG_FLOAT32:
                            fprintf(stderr, "Unable to parse a float32 from unknown argument.\n");
                            return;
                        case ARG_FLOAT64:
                            fprintf(stderr, "Unable to parse a float64 from unknown argument.\n");
                            return;
                        case ARG_USER_DEFINED:
                            fprintf(stderr, "Unable to parse a %s from unknown argument.\n", arg_to_parse->dest.user_pointer->type_name.text);
                            return;
                        case ARG_ENUM:
                            fprintf(stderr, "Unable to parse a choice from unknown argument.\n");
                            return;
                        case ARG_BITFLAG:
                            // fall-through
                        case ARG_FLAG:
                            fprintf(stderr, "Unable to parse a flag. This is a bug.\n");
                            return;
                    }
                    fprintf(stderr, "Unable to parse an unknown type from unknown argument'\n");
                    return;
                }
            }
            else if(parser->failed.arg){
                const char* arg = parser->failed.arg;
                fprintf(stderr, "Unable to parse an unknown type from '%s'\n", arg);
                return;
            }
            else {
                fprintf(stderr, "Unable to parse an unknown type from an unknown argument. This is a bug.\n");
            }
            return;
        case ARGPARSE_UNKNOWN_KWARG:
            if(parser->failed.arg)
                fprintf(stderr, "Unrecognized argument '%s'\n", parser->failed.arg);
            else
                fprintf(stderr, "Unrecognized argument is unknown. This is a bug.\n");
            return;
        case ARGPARSE_DUPLICATE_KWARG:
            fprintf(stderr, "Option given more than once.\n");
            return;
        case ARGPARSE_EXCESS_ARGS:{
            // Args were given after all possible args were consumed.
            if(!parser->failed.arg_to_parse){
                fprintf(stderr, "More arguments given than needed. First excess argument: '%s'.\n", parser->failed.arg);
                return;
            }
            ArgToParse* arg_to_parse = parser->failed.arg_to_parse;

            if(!parser->failed.arg){
                fprintf(stderr, "Excess arguments. No more than %d arguments needed. Unknown first excess argument (this is a bug)\n", agp_maxnum(arg_to_parse->max_num));
                return;
            }
            fprintf(stderr, "Excess arguments. No more than %d arguments needed. First excess argument: '%s'\n", agp_maxnum(arg_to_parse->max_num), parser->failed.arg) ;
        }return;
        case ARGPARSE_INSUFFICIENT_ARGS:{
            if(!parser->failed.arg_to_parse){
                fprintf(stderr, "Insufficent arguments for unknown option. This is a bug.\n");
                return;
            }
            ArgToParse* arg_to_parse = parser->failed.arg_to_parse;
            fprintf(stderr, "Insufficient arguments. %d argument%s required.\n", arg_to_parse->min_num, arg_to_parse->min_num==1?" is":"s are");
        }return;
        case ARGPARSE_VISITED_NO_ARG_GIVEN:{
            ArgToParse* arg_to_parse = parser->failed.arg_to_parse;
            if(!arg_to_parse){
                fprintf(stderr, "An unknown argument was visited. This is a bug.\n");
                return;
            }
            fprintf(stderr, "No arguments given.\n");
        }return;
        case ARGPARSE_INTERNAL_ERROR:{
            fprintf(stderr, "An internal error occurred. This is a bug.\n");
            return;
        }
    }
    fprintf(stderr, "Unknown error when parsing arguments.\n");
    return;
}


static inline
void
print_argparse_single_line_help_escaped(const char* help){
    for(;;help++){
        switch(*help){
            case ' ': case '\t': case '\n':
                continue;
            case 0:
                return;
            default: break;
        }
        break;
    }
    int i = 0;
    for(;*help;help++, i++){
        // if(i > 79) return;
        switch(*help){
        case '"':
            printf("\\\"");
            break;
        case '\t':
        case '\n':
            return;
            // putchar(' ');
        default:
            putchar(*help);
        }
    }
}

static inline
void
print_argparse_fish_completions(const ArgParser* p){
    for(size_t i = 0; i < p->early_out.count; i++){
        ArgToParse* a = &p->early_out.args[i];
        printf("complete -c %s", p->name);
        StringView names[] = {a->name, a->altname1};
        for(size_t j = 0; j < arrlen(names); j++){
            StringView name = names[j];
            if(!name.length) continue;
            if(name.length > 2 && memcmp(name.text, "--", 2) == 0){
                printf(" -l %.*s", (int)name.length-2, name.text+2);
            }
            else if(name.length == 2 && name.text[0] == '-'){
                printf(" -s %.*s", (int)name.length-1, name.text+1);
            }
            else if(name.length > 1 && name.text[0] == '-'){
                printf(" -o %.*s", (int)name.length-1, name.text+1);
            }
        }
        if(a->help){
            printf(" -d \"");
            print_argparse_single_line_help_escaped(a->help);
            putchar('"');
        }
        putchar('\n');
    }
    for(size_t i = 0; i < p->keyword.count; i++){
        ArgToParse* a = &p->keyword.args[i];
        printf("complete -c %s", p->name);
        StringView names[] = {a->name, a->altname1};
        for(size_t j = 0; j < arrlen(names); j++){
            StringView name = names[j];
            if(!name.length) continue;
            if(name.length > 2 && memcmp(name.text, "--", 2) == 0){
                printf(" -l %.*s", (int)name.length-2, name.text+2);
            }
            else if(name.length == 2 && name.text[0] == '-'){
                printf(" -s %.*s", (int)name.length-1, name.text+1);
            }
            else if(name.length > 1 && name.text[0] == '-'){
                printf(" -o %.*s", (int)name.length-1, name.text+1);
            }
        }
        switch(a->dest.type){
            case ARG_FLAG:
            case ARG_BITFLAG:
                break;
            case ARG_FLOAT32:
            case ARG_FLOAT64:
            case ARG_STRING:
            case ARG_CSTRING:
            case ARG_INTEGER64:
            case ARG_UINTEGER64:
            case ARG_USER_DEFINED:
            case ARG_INT:
                printf(" -r");
                break;
            case ARG_ENUM:
                printf(" -a \"");
                for(size_t j = 0; j < a->dest.enum_pointer->enum_count; j++){
                    if(j != 0) putchar(' ');
                    StringView sv = a->dest.enum_pointer->enum_names[j];
                    printf("%.*s", (int)sv.length, sv.text);
                }
                putchar('"');
        }
        if(a->help){
            printf(" -d \"");
            print_argparse_single_line_help_escaped(a->help);
            putchar('"');
        }
        putchar('\n');
    }
}

#ifdef ARGPARSE_EXAMPLE
// This is an example of how to use this header.  To compile, make a .c file,
// #define ARGPARSE_EXAMPLE and include this header.
//
// Or, if you're feeling spicy,
//   `echo '#include "argument_parsing.h"' | cc -DARGPARSE_EXAMPLE -xc -o argparse_example -`
// You may need to add `-I` for whatever directory you put this file in.
int
main(int argc, const char*_Null_unspecified*_Null_unspecified argv){
    Args args = {argc?argc-1:0, argc?argv+1:NULL}; // argc can be zero.
    LongString somepath = LS("");
    LongString output = LS("");
    ArgToParse pos_args[] = {
        [0] = {
            .name = SV("somepath"),
            .min_num = 1,
            .max_num = 1,
            .dest = ARGDEST(&somepath),
            .help = "Source file (.txt file) to read from.",
        },
    };
    int n_times = 5;
    bool dry_run = false;
    ArgToParse kw_args[] = {
        {
            .name = SV("-o"),
            .altname1 = SV("--output"),
            .dest = ARGDEST(&output),
            .help = "Where to write the output file."
        },
        {
            .name = SV("-n"),
            .altname1 = SV("--n-times"),
            .dest = ARGDEST(&n_times),
            .show_default = true,
            .help = "Do it n times.",
        },
        {
            .name = SV("--dry-run"),
            .dest = ARGDEST(&dry_run),
            .help = "Do everything but actually write the file."
        },
    };
    enum {HELP=0, VERSION, FISH};
    ArgToParse early_args[] = {
        [HELP] = {
            .name = SV("-h"),
            .altname1 = SV("--help"),
            .help = "Print this help and exit.",
        },
        [VERSION] = {
            .name = SV("-v"),
            .altname1 = SV("--version"),
            .help = "Print the version and exit.",
        },
        [FISH] = {
            .name = SV("--fish-completions"),
            .help = "Print out commands for fish shell completions.",
            .hidden = true,
        },
    };
    ArgParser parser = {
        .name = argc?argv[0]:"argparse_example",
        .description = "An example of how to use the argparser.",
        .positional.args = pos_args,
        .positional.count = arrlen(pos_args),
        .early_out.args = early_args,
        .early_out.count = arrlen(early_args),
        .keyword.args = kw_args,
        .keyword.count = arrlen(kw_args),
    };
    // Real program would use OS apis to get the width of the terminal.
    int columns = 80;
    switch(check_for_early_out_args(&parser, &args)){
        case HELP:
            print_argparse_help(&parser, columns);
            return 0;
        case VERSION:
            puts("argparse_example v1.2.3");
            return 0;
        case FISH:
            print_argparse_fish_completions(&parser);
            return 0;
        default:
            break;
    }
    enum ArgParseError error = parse_args(&parser, &args, ARGPARSE_FLAGS_NONE);
    if(error){
        print_argparse_error(&parser, error);
        return error;
    }
    // Parsing has succeeded at this point.
    // Real program would then do stuff with these values.
    printf("somepath = '%s'\n", somepath.text);
    if(output.length)
        printf("output = '%s'\n", output.text);
    printf("n_times = %d\n", n_times);
    puts(dry_run? "dry_run = true": "dry_run = false");
    return 0;
}

#endif

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#endif
