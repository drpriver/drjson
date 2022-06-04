#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "measure_time.h"
#include "drjson.h"
#include "Allocators/arena_allocator.h"

static void
print_value(DRJsonValue v, int indent, int pretty){
    switch(v.kind){
        case DRJSON_NUMBER:
            printf("%.12g", v.number); break;
        case DRJSON_INTEGER:
            printf("%lld", v.integer); break;
        case DRJSON_UINTEGER:
            printf("%llu", v.uinteger); break;
        case DRJSON_STRING:
            printf("\"%.*s\"", (int)v.count, v.string); break;
        case DRJSON_ARRAY:{
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
        case DRJSON_OBJECT:{
            putchar('{');
            int newlined = 0;
            for(size_t i = 0; i < v.capacity; i++){
                DRJsonObjectPair* o = &v.object_items[i];
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
        case DRJSON_NULL:
            printf("null"); break;
        case DRJSON_BOOL:
            if(v.boolean)
                printf("true");
            else
                printf("false"); 
            break;
        case DRJSON_ERROR:
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
    DRJsonParseContext ctx = {
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
    long t0 = get_t();
    DRJsonValue v = drjson_parse(&ctx);
    long t1 = get_t();
    printf("%.3f us\n", (double)(t1-t0));
    printf("Kind: %s\n", DRJsonKindNames[v.kind]);
    if(0)
    switch(v.kind){
        case DRJSON_ERROR:
            printf("Error code: %d\n", v.error_code);
            printf("Error mess: %s\n", ctx.error_message);
            printf("Cursor is at: %c\n", *ctx.cursor);
            printf("Off: %ld\n", ctx.cursor - ctx.begin);
            break;
        case DRJSON_OBJECT:{
            DRJsonValue* it_ = drjson_object_get_item(v, "foo", 3, 0);
            if(!it_) break;
            DRJsonValue it = *it_;

            printf("it.Kind: %s\n", DRJsonKindNames[it.kind]);
            switch(it.kind){
                case DRJSON_ERROR:
                    printf("Error code: %d\n", it.error_code);
                    break;
                case DRJSON_NUMBER:
                    printf("v.foo = %f\n", it.number);
                    break;
                default:
                    printf("wtf\n");
                    break;
            }
        }break;
        case DRJSON_ARRAY:
            drjson_array_push_item(&ctx.allocator, &v, drjson_make_int(42));
            drjson_array_push_item(&ctx.allocator, &v, drjson_make_int(12));
            drjson_array_push_item(&ctx.allocator, &v, drjson_make_int(8));
            drjson_array_push_item(&ctx.allocator, &v, drjson_make_int(27));
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
    DRJsonValue* it = drjson_query(v, query, qlen);
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
        drjson_slow_recursive_free_all(&ctx.allocator, v);
    }

    return 0;
}

#include "drjson.c"
#include "Allocators/allocator.c"
