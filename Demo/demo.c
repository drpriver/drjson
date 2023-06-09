#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "DrJson/drjson.h"
#include "arena_allocator.h"

static inline
char*
read_file_streamed(FILE* fp){
    size_t used = 0;
    size_t nalloced = 1024;
    char* buff = malloc(nalloced);
    if(!buff) goto fail;
    for(;;){
        size_t remainder = nalloced - used;
        size_t nread = fread(buff+used, 1, remainder, fp);
        if(nread == remainder){
            nalloced *= 2;
            char* newbuff = realloc(buff, nalloced);
            if(!newbuff) goto fail;
            buff = newbuff;
        }
        used += nread;
        if(nread != remainder){
            if(feof(fp)) break;
            else goto fail;
        }
    }
    {
        char* newbuff = realloc(buff, used+1);
        if(!newbuff) goto fail;
        buff = newbuff;
    }
    buff[used] = 0;
    return buff;

    fail:
    free(buff);
    return NULL;
}

int main(int argc, char** argv){
    const char* data = ""
        "{\n"
        "    foo: 123.4e12\n"
        "}\n";
    size_t nbytes = strlen(data);
    if(argc > 1){
        char* arg = argv[1];
        FILE* fp = fopen(arg, "rb");
        if(!fp) {
            data = arg;
            nbytes = strlen(data);
        }
        else {
            data = read_file_streamed(fp);
            if(!data) return 1;
            nbytes = strlen(data);
            fclose(fp);
        }
    }

    ArenaAllocator aa = {0};
    DrJsonAllocator allocator = {
        .user_pointer = &aa,
        .alloc = (void*(*)(void*, size_t))ArenaAllocator_alloc,
        .realloc = (void*(*)(void*, void*, size_t, size_t))ArenaAllocator_realloc,
        .free = (void(*)(void*, const void*, size_t))ArenaAllocator_free,
        .free_all = (void(*)(void*))ArenaAllocator_free_all,
    };
    DrJsonContext* jctx = drjson_create_ctx(allocator);

    DrJsonParseContext ctx = {
        .ctx = jctx,
        .begin = data,
        .cursor = data,
        .end = data + nbytes,
    };
    // We know our string lives longer than the ctx.
    unsigned flags = DRJSON_PARSE_FLAG_NO_COPY_STRINGS;
    DrJsonValue v = drjson_parse(&ctx, flags);
    if(v.kind == DRJSON_ERROR){
        size_t l, c;
        drjson_get_line_column(&ctx, &l, &c);
        drjson_print_error_fp(stderr, "input", 5, l, c, v);
        return 1;
    }
    if(argc <= 2){
        drjson_print_value_fp(jctx, stdout, v, 0, DRJSON_PRETTY_PRINT);
        putchar('\n');
        return 0;
    }
    const char* query = "";
    if(argc > 2)
        query = argv[2];
    size_t qlen = strlen(query);
    if(qlen){
        if(argc > 3){
            DrJsonValue it = drjson_checked_query(jctx, v, atoi(argv[3]), query, qlen);
            drjson_print_value_fp(jctx, stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
            return 0;
        }
        DrJsonValue it = drjson_query(jctx, v, query, qlen);
        if(it.kind != DRJSON_ERROR){
            drjson_print_value_fp(jctx, stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
        }
        else {
            drjson_print_value_fp(jctx, stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
        }
    }
    // We're returning anyway, but this is how you de-allocate memory allocated
    // in the ctx.
    drjson_ctx_free_all(jctx);
    return 0;
}

// this is unused, it's just to see if the README compiles
int
write_foo_bar_baz_to_fp(const char* json, size_t length, FILE* fp){
  int result = 0;
  DrJsonContext* ctx = drjson_create_ctx(drjson_stdc_allocator());
  DrJsonParseContext parsectx = {
    .begin=json,
    .cursor=json,
    .end=json+length,
    .ctx = ctx,
  };
  unsigned flags = DRJSON_PARSE_FLAG_NO_COPY_STRINGS;
  DrJsonValue v = drjson_parse(&parsectx, flags);
  if(v.kind == DRJSON_ERROR){
    result = 1;
    goto done;
  }
  DrJsonValue o = drjson_query(ctx, v, "foo.bar.baz", sizeof("foo.bar.baz")-1);
  if(o.kind == DRJSON_ERROR){
    result = 2;
    goto done;
  }
  int indent = 0;
  int err = drjson_print_value_fp(ctx, fp, o, indent, DRJSON_PRETTY_PRINT);
  if(err){
    result = 3;
    goto done;
  }
  done:
  drjson_ctx_free_all(ctx);
  return result;
}
