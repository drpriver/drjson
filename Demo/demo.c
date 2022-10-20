#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "DrJson/drjson.h"
#include "arena_allocator.h"

static inline
char*
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
                return NULL;
            }
            buff = newbuff;
        }
        used += nread;
        if(nread != remainder){
            if(feof(fp)) break;
            else{
                free(buff);
                return NULL;
            }
        }
    }
    buff = realloc(buff, used+1);
    buff[used] = 0;
    return buff;
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
    DrJsonContext jctx = {
        .allocator = {
            .user_pointer = &aa,
            .alloc = (void*(*)(void*, size_t))ArenaAllocator_alloc,
            .realloc = (void*(*)(void*, void*, size_t, size_t))ArenaAllocator_realloc,
            .free = (void(*)(void*, const void*, size_t))ArenaAllocator_free,
            .free_all = (void(*)(void*))ArenaAllocator_free_all,
        },
    };

    DrJsonParseContext ctx = {
        .ctx = &jctx,
        .begin = data,
        .cursor = data,
        .end = data + nbytes,
    };
    DrJsonValue v = drjson_parse(&ctx);
    if(drjson_kind(v) == DRJSON_ERROR){
        size_t l, c;
        drjson_get_line_column(&ctx, &l, &c);
        drjson_print_error_fp(stderr, "input", 5, l, c, v);
        return 1;
    }
    if(argc <= 2){
        drjson_print_value_fp(&jctx, stdout, v, 0, DRJSON_PRETTY_PRINT);
        putchar('\n');
        return 0;
    }
    const char* query = "";
    if(argc > 2)
        query = argv[2];
    size_t qlen = strlen(query);
    if(qlen){
        if(argc > 3){
            DrJsonValue it = drjson_checked_query(&jctx, v, atoi(argv[3]), query, qlen);
            drjson_print_value_fp(&jctx, stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
            return 0;
        }
        DrJsonValue it = drjson_query(&jctx, v, query, qlen);
        if(drjson_kind(it) != DRJSON_ERROR){
            // printf("v%s: ", query);
            drjson_print_value_fp(&jctx, stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
        }
        else {
            drjson_print_value_fp(&jctx, stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
        }
    }
    if(jctx.allocator.free_all)
        jctx.allocator.free_all(jctx.allocator.user_pointer);

    return 0;
}

// this is unused, it's just to see if the README compiles
int
write_foo_bar_baz_to_fp(const char* json, size_t length, FILE* fp){
  int result = 0;
  DrJsonContext jctx = {
      .allocator = drjson_stdc_allocator(),
  };
  DrJsonParseContext ctx = {
    .begin=json,
    .cursor=json,
    .end=json+length,
    .ctx = &jctx,
  };
  DrJsonValue v = drjson_parse(&ctx);
  if(drjson_kind(v) == DRJSON_ERROR){
    result = 1;
    goto done;
  }
  DrJsonValue o = drjson_query(&jctx, v, "foo.bar.baz", sizeof("foo.bar.baz")-1);
  if(drjson_kind(o) == DRJSON_ERROR){
    result = 2;
    goto done;
  }
  int err = drjson_print_value_fp(&jctx, fp, o, 0, DRJSON_PRETTY_PRINT);
  if(err){
    result = 3;
    goto done;
  }
  done:
  drjson_ctx_free_all(&jctx);
  return result;
}
