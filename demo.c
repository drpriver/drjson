#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "measure_time.h"
#include "drjson.h"
#include "Allocators/arena_allocator.h"

static void
print_value(CJsonValue v, int indent, int pretty){
    switch(v.kind){
        case CJSON_NUMBER:
            printf("%.12g", v.number); break;
        case CJSON_INTEGER:
            printf("%lld", v.integer); break;
        case CJSON_UINTEGER:
            printf("%llu", v.uinteger); break;
        case CJSON_STRING:
            printf("\"%.*s\"", (int)v.count, v.string); break;
        case CJSON_ARRAY:{
            putchar('[');
            if(pretty)
            if(v.count)
                putchar('\n');
            int newlined = 0;
            for(size_t i = 0; i < v.count; i++){
                if(pretty)
                for(int i = 0; i < indent+2; i++)
                    putchar(' ');
                print_value(v.array_items[i], indent+2, pretty);
                if(i != v.count-1)
                    putchar(',');
                if(pretty)
                    putchar('\n');
                newlined = 1;
            }
            if(pretty)
            if(v.count){
                for(int i = 0; i < indent; i++)
                    putchar(' ');
            }
            putchar(']');
        }break;
        case CJSON_OBJECT:{
            putchar('{');
            int newlined = 0;
            for(size_t i = 0; i < v.capacity; i++){
                CJsonObjectPair* o = &v.object_items[i];
                if(!o->key) continue;
                if(newlined)
                putchar(',');
                if(pretty)
                putchar('\n');
                newlined = 1;
                if(pretty)
                for(int ind = 0; ind < indent+2; ind++)
                    putchar(' ');
                printf("\"%.*s\":", (int)o->key_length, o->key);
                print_value(o->value, indent+2, pretty);

            }
            if(pretty)
            if(newlined) putchar('\n');
            if(pretty)
            for(int i = 0; i < indent; i++)
                putchar(' ');
            putchar('}');
        }break;
        case CJSON_NULL:
            printf("null"); break;
        case CJSON_BOOL:
            if(v.boolean)
                printf("true");
            else
                printf("false"); 
            break;
        case CJSON_ERROR:
            printf("Error"); break;
    }

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
            fseek(fp, 0, SEEK_END);
            nbytes = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char* d = malloc(nbytes);
            fread(d, nbytes, 1, fp);
            fclose(fp);
            data = d;
        }
    }

    ArenaAllocator aa = {0};

    for(int i = 0; i < 1; i++){
    CJsonParseContext ctx = {
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
        // .allocator = cjson_stdc_allocator(),
    };
    long t0 = get_t();
    CJsonValue v = cjson_parse(&ctx);
    long t1 = get_t();
    printf("%.3f us\n", (double)(t1-t0));
    printf("Kind: %s\n", CJsonKindNames[v.kind]);
    if(0)
    switch(v.kind){
        case CJSON_ERROR:
            printf("Error code: %d\n", v.error_code);
            printf("Error mess: %s\n", ctx.error_message);
            printf("Cursor is at: %c\n", *ctx.cursor);
            printf("Off: %ld\n", ctx.cursor - ctx.begin);
            break;
        case CJSON_OBJECT:{
            CJsonValue* it_ = cjson_object_get_item(v, "foo", 3, 0);
            if(!it_) break;
            CJsonValue it = *it_;

            printf("it.Kind: %s\n", CJsonKindNames[it.kind]);
            switch(it.kind){
                case CJSON_ERROR:
                    printf("Error code: %d\n", it.error_code);
                    break;
                case CJSON_NUMBER:
                    printf("v.foo = %f\n", it.number);
                    break;
                default:
                    printf("wtf\n");
                    break;
            }
        }break;
        case CJSON_ARRAY:
            cjson_array_push_item(&ctx.allocator, &v, cjson_make_int(42));
            cjson_array_push_item(&ctx.allocator, &v, cjson_make_int(12));
            cjson_array_push_item(&ctx.allocator, &v, cjson_make_int(8));
            cjson_array_push_item(&ctx.allocator, &v, cjson_make_int(27));
            break;
        default:
            printf("Uh what the fuck\n");
            break;
    }
    // print_value(v, 0, 0);
    // putchar('\n');
    const char* query = "[3]";
    if(argc > 2)
        query = argv[2];
    size_t qlen = strlen(query);
    CJsonValue* it = cjson_query(v, query, qlen);
    if(it){
        // printf("v%s: ", query);
        print_value(*it, 0, 1);
        putchar('\n');
    }
    else {
        printf("query went wrong...\n");
    }
    if(ctx.allocator.free_all)
        ctx.allocator.free_all(ctx.allocator.user_pointer);
    else
        cjson_slow_recursive_free_all(&ctx.allocator, v);
    }

    return 0;
}

#include "drjson.c"
#include "Allocators/allocator.c"
