//
// Copyright © 2022-2024, David Priver <david@davidpriver.com>
//
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#define DRJSON_API static inline
#include "drjson.h"
// "drjson.c" is #included at the bottom
#include "argument_parsing.h"
#include "term_util.h"
#include "get_input.h"

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define force_inline static inline __forceinline
#else
#define force_inline static inline
#endif
#endif

// Chose to use libc's FILE api instead of OS APIs for portability.

force_inline
warn_unused
long long
file_size_from_fp(FILE* fp){
    long long result = 0;
    // sadly, the only way in standard c to do this.
    if(fseek(fp, 0, SEEK_END))
        goto errored;
    result = ftell(fp);
    if(result < 0)
        goto errored;
    if(fseek(fp, 0, SEEK_SET))
        goto errored;
    return result;

    errored:
    return -1;
}



static inline
int
read_file_streamed(FILE* fp, LongString* out){
    size_t nalloced = 1024;
    size_t used = 0;
    char* buff = malloc(nalloced);
    for(;;){
        size_t remainder = nalloced - used;
        size_t nread = fread(buff+used, 1, remainder, fp);
        if(nread == remainder){
            nalloced *= 2;
            char* newbuff = realloc(buff, nalloced);
            if(!newbuff){
                free(buff);
                return -1;
            }
            buff = newbuff;
        }
        used += nread;
        if(nread != remainder){
            if(feof(fp)) break;
            else{
                free(buff);
                return -1;
            }
        }
    }
    buff = realloc(buff, used+1);
    buff[used] = 0;
    *out = (LongString){used, buff};
    return 0;
}

static inline
int
read_file(const char* filepath, LongString* out){
    int status = -1;
    FILE* fp = fopen(filepath, "rb");
    if(!fp) return -1;
    long long size = file_size_from_fp(fp);
    if(size <= 0){
        status = read_file_streamed(fp, out);
        goto finally;
    }
    size_t nbytes = size;
    char* text = malloc(nbytes+1);
    if(!text) goto finally;
    size_t fread_result = fread(text, 1, nbytes, fp);
    if(fread_result != nbytes){
        free(text);
        goto finally;
    }
    text[nbytes] = '\0';
    *out = (LongString){nbytes, text};
    status = 0;
finally:
    fclose(fp);
    return status;
}

static GiTabCompletionFunc drj_completer;
typedef struct DrjCompleterCtx DrjCompleterCtx;
struct DrjCompleterCtx {
    DrJsonContext* ctx;
    DrJsonValue* v;
};

int 
main(int argc, const char* const* argv){
    Args args = {argc?argc-1:0, argc?argv+1:NULL};
    LongString jsonpath = {0};
    ArgToParse pos_args[] = {
        [0] = {
            .name = SV("filepath"),
            .min_num = 0,
            .max_num = 1,
            .dest = ARGDEST(&jsonpath),
            .help = "Json file to parse",
        },
    };

    LongString outpath = {0};
    LongString queries[100];
    enum {QUERY_KWARG=1};
    _Bool braceless = 0;
    _Bool ndjson = 0;
    _Bool pretty = 0;
    _Bool interactive = 0;
    _Bool intern = 0;
    _Bool gc = 0;
    int indent = 0;
    ArgToParse kw_args[] = {
        {
            .name = SV("-o"),
            .altname1 = SV("--output"),
            .dest = ARGDEST(&outpath),
            .help = "Where to write the result",
        },
        [QUERY_KWARG] = {
            .name = SV("-q"),
            .altname1 = SV("--query"),
            .min_num=0,
            .max_num=arrlen(queries),
            .dest = ARGDEST(&queries[0]),
            .help = "A query to filter the data. Queries can be stacked",
        },
        {
            .name = SV("--braceless"),
            .dest = ARGDEST(&braceless),
            .help = "Don't require opening and closing braces around the document",
        },
        {
            .name = SV("--ndjson"),
            .dest = ARGDEST(&ndjson),
            .help = "Parse newline-delimited JSON (multiple top-level values into an array)",
        },
        {
            .name = SV("-p"),
            .altname1 = SV("--pretty"),
            .dest = ARGDEST(&pretty),
            .help = "Pretty print the output",
        },
        {
            .name = SV("--indent"),
            .dest = ARGDEST(&indent),
            .help = "Number of leading spaces to print",
        },
        {
            .name = SV("-i"),
            .altname1 = SV("--interactive"),
            .help = "Enter a cli prompt",
            .dest = ARGDEST(&interactive),
        },
        {
            .name = SV("--intern-objects"),
            .altname1 = SV("--intern"),
            .help = "Reuse duplicate arrays and objects while parsing. Slower but can use less memory. Sometimes.",
            .dest = ARGDEST(&intern),
            .hidden = 1,
        },
        {
            .name = SV("--gc"),
            .help = "Run the gc on exit. This is for testing.",
            .dest = ARGDEST(&gc),
            .hidden = 1,
        },
    };
    enum {HELP=0, HIDDEN_HELP, VERSION, FISH};
    ArgToParse early_args[] = {
        [HELP] = {
            .name = SV("-h"),
            .altname1 = SV("--help"),
            .help = "Print this help and exit.",
        },
        [HIDDEN_HELP] = {
            .name = SV("-H"),
            .altname1 = SV("--hidden-help"),
            .help = "Print this help and exit.",
            .hidden = 1,
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
        .name = argc?argv[0]:"drjson",
        .description = "CLI interface to drjson.",
        .positional.args = pos_args,
        .positional.count = arrlen(pos_args),
        .early_out.args = early_args,
        .early_out.count = arrlen(early_args),
        .keyword.args = kw_args,
        .keyword.count = arrlen(kw_args),
        .styling.plain = !isatty(fileno(stdout)),
    };
    int columns = get_terminal_size().columns;
    switch(check_for_early_out_args(&parser, &args)){
        case HELP:
            print_argparse_help(&parser, columns);
            return 0;
        case HIDDEN_HELP:
            print_argparse_hidden_help(&parser, columns);
            return 0;
        case VERSION:
            puts("drjson v" DRJSON_VERSION);
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
    if(indent < 0)
        indent = 0;
    if(indent > 80)
        indent = 80;
    if(indent)
        pretty = 1;
    LongString jsonstr = {0};
    int read_status = -1;
    if(jsonpath.length){
        read_status = read_file(jsonpath.text, &jsonstr);
        if(read_status != 0){
            fprintf(stderr, "Unable to read data from '%s': %s\n", jsonpath.text, strerror(errno));
            return 1;
        }
    }
    else {
        read_status = read_file_streamed(stdin, &jsonstr);
        if(read_status != 0){
            fprintf(stderr, "Unable to read data from stdin: %s\n", strerror(errno));
            return 1;
        }
    }
    DrJsonAllocator allocator = drjson_stdc_allocator();
    DrJsonContext* jctx = drjson_create_ctx(allocator);
    DrJsonParseContext ctx = {
        .ctx = jctx,
        .begin = jsonstr.text,
        .cursor = jsonstr.text,
        .end = jsonstr.text+jsonstr.length,
        .depth = 0,
    };
    unsigned flags = DRJSON_PARSE_FLAG_NONE;
    if(braceless) flags |= DRJSON_PARSE_FLAG_BRACELESS_OBJECT;
    if(ndjson) flags |= DRJSON_PARSE_FLAG_NDJSON;
    if(intern) flags |= DRJSON_PARSE_FLAG_INTERN_OBJECTS;
    flags |= DRJSON_PARSE_FLAG_NO_COPY_STRINGS;
    DrJsonValue document = drjson_parse(&ctx, flags);
    if(document.kind == DRJSON_ERROR){
        size_t l, c;
        drjson_get_line_column(&ctx, &l, &c);
        drjson_print_error_fp(stderr,  jsonpath.text, jsonpath.length, l, c, document);
        return 1;
    }
    if(interactive){
        DrJsonValue this = document;
        DrJsonValue stack[1024] = {this};
        size_t top = 0;
        DrjCompleterCtx dj = {.v = &this, .ctx=jctx};
        for(int i = 0; i < kw_args[QUERY_KWARG].num_parsed; i++){
            DrJsonValue v = drjson_query(jctx, this, queries[i].text, queries[i].length);
            if(v.kind == DRJSON_ERROR){
                fprintf(stderr, "Error when evaluating the %dth query ('%s'): ", i, queries[i].text);
                drjson_print_value_fp(jctx, stderr, v, 0, DRJSON_PRETTY_PRINT|DRJSON_APPEND_NEWLINE);
                return 1;
            }
            this = v;
            stack[++top] = this;
        }
        char prompt[1024];
        GetInputCtx gi = {
            .prompt=SV("> "),
            .tab_completion_func = drj_completer,
            .tab_completion_user_data = &dj,
        };
        for(;;){
            int n = snprintf(prompt, sizeof prompt, "%s %zu) ", drjson_kind_name(this.kind, NULL), top);
            gi.prompt = (StringView){n, prompt};
            ssize_t len = gi_get_input(&gi);
            if(len < 0) break;
            fputs("\r", stdout);
            char* cmd = gi.buff;
            // strip spaces
            for(;;){
                switch(*cmd){
                    case ' ':
                        cmd++;
                        len--;
                        continue;
                    break;
                }
                break;
            }
            for(;len;){
                switch(cmd[len-1]){
                    case ' ':
                        len--;
                        continue;
                    break;
                }
                break;
            }
            if(!len) continue;
            gi_add_line_to_history_len(&gi, cmd, len);
            StringView sv = {len, cmd};
            if(SV_equals(sv, SV("reset"))){
                this = document;
                continue;
            }
            if(SV_equals(sv, SV("q")) || SV_equals(sv, SV("quit"))){
                break;
                continue;
            }
            if(SV_equals(sv, SV("print")) || SV_equals(sv, SV("p"))){
                drjson_print_value_fp(jctx, stdout, this, 0, DRJSON_PRETTY_PRINT|DRJSON_APPEND_NEWLINE);
                continue;
            }
            if(SV_equals(sv, SV("pop")) || SV_equals(sv, SV("up")) || SV_equals(sv, SV("cd .."))){
                if(top)
                    this = stack[--top];
                continue;
            }
            if(SV_equals(sv, SV("ls"))){
                if(this.kind == DRJSON_ARRAY){
                    sv = SV("@length");
                }
                else
                    sv = SV("@keys");
            }
            if(SV_equals(sv, SV("h")) || SV_equals(sv, SV("help"))){
                puts(
                    "reset: restores the current value to the global document\n"
                    "quit: quits\n"
                    "print: prints the current value\n"
                    "push <query>, cd <query>: sets the current value to the result of the query (if successful)\n"
                    "pop, up: pops the stack\n"
                    "ls: print current keys\n"
                    "gc: runs the gc\n"
                    "<query>: prints the result of the query\n");
                continue;
            }
            if(SV_equals(sv, SV("gc"))){
                drjson_gc(jctx, stack, top+1);
                continue;
            }
            _Bool push = 0;
            if(sv.length > 5 && SV_equals((StringView){5, cmd}, SV("push "))){
                sv = (StringView){len-5, cmd+5};
                push = 1;
            }
            if(sv.length > 3 && SV_equals((StringView){3, cmd}, SV("cd "))){
                sv = (StringView){len-3, cmd+3};
                push = 1;
            }
            if(sv.length > 6 && SV_equals((StringView){6, cmd}, SV("print "))){
                sv = (StringView){len-6, cmd+6};
            }
            if(sv.length > 2 && SV_equals((StringView){2, cmd}, SV("p "))){
                sv = (StringView){len-2, cmd+2};
            }
            DrJsonValue v = drjson_query(jctx, this, sv.text, sv.length);
            if(v.kind == DRJSON_ERROR){
                fprintf(stderr, "\rError\n");
                continue;
            }
            if(push){
                this = v;
                if(top < arrlen(stack)){
                    stack[++top] = this;
                }
                continue;
            }
            drjson_print_value_fp(jctx, stdout, v, 0, DRJSON_PRETTY_PRINT|DRJSON_APPEND_NEWLINE);
        }
        return 0;
    }
    int nqueries = kw_args[QUERY_KWARG].num_parsed;
    DrJsonValue result = document;
    if(nqueries){
        for(int i = 0; i < nqueries; i++){
            result = drjson_query(jctx, result, queries[i].text, queries[i].length);
            if(result.kind == DRJSON_ERROR){
                fprintf(stderr, "Error when evaluating the %dth query ('%s'): ", i, queries[i].text);
                drjson_print_value_fp(jctx, stderr, result, 0, DRJSON_PRETTY_PRINT|DRJSON_APPEND_NEWLINE);
                return 1;
            }
        }
    }
    FILE* outfp = stdout;
    if(outpath.length){
        outfp = fopen(outpath.text, "wb");
        if(!outfp){
            fprintf(stderr, "Unable to open '%s' for writing: %s\n", outpath.text, strerror(errno));
            return 1;
        }
    }
    int err = drjson_print_value_fp(jctx, outfp, result, indent, DRJSON_APPEND_NEWLINE|(pretty?DRJSON_PRETTY_PRINT:0)|(braceless?DRJSON_PRINT_BRACELESS:0)|(ndjson?DRJSON_PRINT_NDJSON:0));
    if(err){
        fprintf(stderr, "err when writing: %d\n", err);
    }
    fflush(outfp);
    fclose(outfp);
    if(gc) drjson_gc(jctx, 0, 0);
    return err;
}

static inline
_Bool
SV_startswith(StringView hay, StringView needle){
    if(needle.length > hay.length) return 0;
    if(!needle.length) return 1;
    return memcmp(needle.text, hay.text, needle.length) == 0;
}


static
int
drj_completer(GetInputCtx* ctx, size_t original_curr_pos, size_t original_used_len, int n_tabs){
    static int n_strs;
    static StringView key_svs[1024];
    static ptrdiff_t prefix;

    if(!ctx->tab_completion_user_data) return -1;
    if(original_curr_pos != original_used_len) return 0;
    DrjCompleterCtx* dj = ctx->tab_completion_user_data;

    if(n_tabs == 1){ // first one
        prefix = 0;
        n_strs = 0;
        char* space = strrchr(ctx->buff, ' ');
        if(space){
            prefix = space+1 - ctx->buff;
        }
        DrJsonValue keys = drjson_object_keys(*dj->v);
        if(keys.kind == DRJSON_ERROR) return 0;
        int64_t len = drjson_len(dj->ctx, keys);
        if(len <= 0) return 0;
        StringView buffview = {original_used_len-prefix, ctx->buff+prefix};
        for(int64_t i = 0; i < len; i++){
            if(n_strs == arrlen(key_svs)) break;
            DrJsonValue k = drjson_get_by_index(dj->ctx, keys, i);
            if(k.kind == DRJSON_ERROR) return 0;
            if(k.kind != DRJSON_STRING) return 0;
            StringView sv = SV("");;
            int err = drjson_get_str_and_len(dj->ctx, k, &sv.text, &sv.length);
            (void)err;
            if(!SV_startswith(sv, buffview)) continue;
            key_svs[n_strs++] = sv;
        }
        if(n_strs < (int)arrlen(key_svs) && SV_startswith(SV("@keys"), buffview)){
            key_svs[n_strs++] = SV("@keys");
        }
        if(n_strs < (int)arrlen(key_svs) && SV_startswith(SV("@length"), buffview)){
            key_svs[n_strs++] = SV("@length");
        }
        qsort(key_svs, n_strs, sizeof key_svs[0], StringView_cmp);
    }
    if((n_tabs % (n_strs+1))==0){
        memcpy(ctx->buff, ctx->altbuff, original_used_len);
        ctx->buff_count = original_used_len;
        ctx->buff_cursor = original_curr_pos;
        ctx->buff[original_used_len] = 0;
        ctx->tab_completion_cookie = 0;
        return 0;
    }
    StringView k = key_svs[(n_tabs-1)%(n_strs+1)];
    if(k.length+prefix >= sizeof ctx->buff) return 0;
    memcpy(ctx->buff+prefix, k.text, k.length);
    ctx->buff[prefix+k.length] = 0;
    ctx->buff_count = prefix+k.length;
    ctx->buff_cursor = prefix+k.length;
    return 0;
}

#include "drjson.c"
#include "get_input.c"

