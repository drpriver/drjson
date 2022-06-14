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
    const char* data = "{\n"
        "foo: 123.4e12\n"
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

    DrJsonParseContext ctx = {
        .begin = data,
        .cursor = data,
        .end = data + nbytes, 
        .allocator = {
            .user_pointer = &aa,
            .alloc = (void*(*)(void*, size_t))ArenaAllocator_alloc,
            .realloc = (void*(*)(void*, void*, size_t, size_t))ArenaAllocator_realloc,
            .free = (void(*)(void*, const void*, size_t))ArenaAllocator_free,
            .free_all = (void(*)(void*))ArenaAllocator_free_all,
        },
        // .allocator = drjson_stdc_allocator(),
    };
    DrJsonValue v = drjson_parse(&ctx);
    if(v.kind == DRJSON_ERROR){
        fprintf(stderr, "%s (%d): %s\n", drjson_get_error_name(v), drjson_get_error_code(v), v.err_mess);
        return 1;
    }
    if(argc <= 2){
        drjson_print_value(stdout, v, 0, DRJSON_PRETTY_PRINT);
        putchar('\n');
        return 0;
    }
    const char* query = "";
    if(argc > 2)
        query = argv[2];
    size_t qlen = strlen(query);
    if(qlen){
        if(argc > 3){
            DrJsonValue it = drjson_checked_query(&v, atoi(argv[3]), query, qlen);
            drjson_print_value(stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
            return 0;
        }
        DrJsonValue it = drjson_query(&v, query, qlen);
        if(it.kind != DRJSON_ERROR){
            // printf("v%s: ", query);
            drjson_print_value(stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
        }
        else {
            it = drjson_multi_query(&ctx.allocator, &v, query, qlen);
            drjson_print_value(stdout, it, 0, DRJSON_PRETTY_PRINT);
            putchar('\n');
        }
    }
    if(ctx.allocator.free_all)
        ctx.allocator.free_all(ctx.allocator.user_pointer);
    else
        drjson_slow_recursive_free_all(&ctx.allocator, v);

    return 0;
}

// this is unused, it's just to see if the README compiles
int 
write_foo_bar_baz_to_fp(const char* json, size_t length, FILE* fp){
  DrJsonParseContext ctx = {
    .begin=json, 
    .cursor=json, 
    .end=json+length, 
    .allocator=drjson_stdc_allocator(),
  };
  DrJsonValue v = drjson_parse(&ctx);
  if(v.kind == DRJSON_ERROR){
    // handle error
    return 1;
  }
  DrJsonValue o = drjson_query(&v, "foo.bar.baz", sizeof("foo.bar.baz")-1);
  if(o.kind != DRJSON_BOXED){
    // handle error
    drjson_slow_recursive_free_all(&ctx.allocator, v);
    return 2;
  }
  int err = drjson_print_value(fp, *o.boxed, 0, DRJSON_PRETTY_PRINT);
  if(err){
    // handle error
     drjson_slow_recursive_free_all(&ctx.allocator, v);
     return 3;
  }
  drjson_slow_recursive_free_all(&ctx.allocator, v);
  return 0;
}
