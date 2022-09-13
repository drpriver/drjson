//
// Copyright © 2022, David Priver
//
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
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

static inline
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
LongString
read_file_streamed(FILE* fp){
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
                return (LongString){0};
            }
            buff = newbuff;
        }
        used += nread;
        if(nread != remainder){
            if(feof(fp)) break;
            else{
                free(buff);
                return (LongString){0};
            }
        }
    }
    buff = realloc(buff, used+1);
    buff[used] = 0;
    return (LongString){used, buff};
}

static inline
LongString 
read_file(const char* filepath){
    LongString result = {0};
    FILE* fp = fopen(filepath, "rb");
    if(!fp) return result;
    long long size = file_size_from_fp(fp);
    if(size <= 0){
        result = read_file_streamed(fp);
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
    result.text = text;
    result.length = nbytes;
finally:
    fclose(fp);
    return result;
}


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
    _Bool pretty = 0;
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
            .max_num=100,
            .dest = ARGDEST(&queries[0]),
            .help = "A query to filter the data. Queries can be stacked",
        },
        {
            .name = SV("--braceless"),
            .dest = ARGDEST(&braceless),
            .help = "Don't require opening and closing braces around the document",
        },
        {
            .name = SV("-p"),
            .altname1 = SV("--pretty"),
            .dest = ARGDEST(&pretty),
            .help = "Pretty print the output",
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
        .name = argc?argv[0]:"drjson",
        .description = "CLI interface to drjson.",
        .positional.args = pos_args,
        .positional.count = arrlen(pos_args),
        .early_out.args = early_args,
        .early_out.count = arrlen(early_args),
        .keyword.args = kw_args,
        .keyword.count = arrlen(kw_args),
    };
    int columns = get_terminal_size().columns;
    switch(check_for_early_out_args(&parser, &args)){
        case HELP:
            print_argparse_help(&parser, columns);
            return 0;
        case VERSION:
            puts("drjson v1.0.0");
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
    LongString jsonstr = {0};
    if(jsonpath.length){
        jsonstr = read_file(jsonpath.text);
        if(!jsonstr.text){
            fprintf(stderr, "Unable to read data from '%s': %s\n", jsonpath.text, strerror(errno));
            return 1;
        }
    }
    else {
        jsonstr = read_file_streamed(stdin);
    }
    DrJsonAllocator allocator = drjson_stdc_allocator();
    DrJsonParseContext ctx = {
        .allocator = allocator,
        .begin = jsonstr.text,
        .cursor = jsonstr.text,
        .end = jsonstr.text+jsonstr.length,
        .depth = 0,
    };
    DrJsonValue document = braceless?
        drjson_parse_braceless_object(&ctx):
        drjson_parse(&ctx);
    if(document.kind == DRJSON_ERROR){
        drjson_print_error_fp(stderr,  jsonpath.text, jsonpath.length, &ctx, document);
        return 1;
    }
    int nqueries = kw_args[QUERY_KWARG].num_parsed;
    DrJsonValue* result = &document;
    DrJsonValue query_results[100];
    if(nqueries){
        for(int i = 0; i < nqueries; i++){
            DrJsonValue qresult = drjson_multi_query(&allocator, result, queries[i].text, queries[i].length);
            if(qresult.kind == DRJSON_ERROR){
                fprintf(stderr, "Error when evaluating the %dth query ('%s'):", i, queries[i].text);
                drjson_print_value_fp(stderr, qresult, 0, DRJSON_PRETTY_PRINT|DRJSON_APPEND_NEWLINE);
                return 1;
            }
            query_results[i] = qresult;
            result = &query_results[i];
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
    int err = drjson_print_value_fp(outfp, *result, 0, DRJSON_APPEND_NEWLINE|(pretty?DRJSON_PRETTY_PRINT:0));
    if(err){
        fprintf(stderr, "err when writing: %d\n", err);
    }
    fflush(outfp);
    fclose(outfp);
    return err;
}

#include "drjson.c"

