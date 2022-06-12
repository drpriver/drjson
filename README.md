# DrJson

DrJson is a liberal json parsing and querying library for C.
It provides functions that parse a json document as well as a simple query
language for retrieving values.  It also provides a json printer/pretty-printer.

## Building

A meson.build and a CMakeLists.txt are provided for building. Or you can use
the Makefile, but that is primarily for development.

## Usage

```C

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
```

It is recommended instead to provide your own allocator that provides an
efficient `free_all` function instead of having to deal with the
`drjson_slow_rescurive_free_all`.
See [`Demo/arena_allocator.h`](Demo/arena_allocator.h) for an example.

