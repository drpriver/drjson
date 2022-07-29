# DrJson

DrJson is a liberal json parsing and querying library for C.
It provides functions that parse a json document as well as a simple query
language for retrieving values.  It also provides a json printer/pretty-printer.

## Building

A meson.build and a CMakeLists.txt are provided for building. Or you can use
the Makefile, but that is primarily for development.

## Usage

### CLI
```
$ drjson -h
drjson: CLI interface to drjson.

usage: drjson filepath [-o | --output <string>]
              [-q | --query <string> ...] [--braceless] [--pretty]

Early Out Arguments:
--------------------
-h, --help:
    Print this help and exit. 
-v, --version:
    Print the version and exit. 

Positional Arguments:
---------------------
filepath <string>
    Json file to parse 

Keyword Arguments:
------------------
-o, --output <string>
    Where to write the result 
-q, --query <string> ... 
    A query to filter the data. Queries can be stacked 
--braceless
    Don't require opening and closing braces around the document 
--pretty
    Pretty print the output 

```
<hr>

Query a specific field from a large file:
```
$ drjson some_big_file.json -q some_metadata.classes --pretty
[
  "cat",
  "dog",
  "ferret",
  "cow",
]
```
<hr>

Convert a djrson file to regular json:
```
$ drjson Examples/settings.drjson --braceless --pretty
{
  "font": "Sans",
  "graphic-color": 4288729810,
  "text-size": 18,
  "bg-color": 4280494119,
  "damage": [
    "d6",
    "fire"
  ],
  "misc-color": 4288729810,
  "console-color": 4288729810,
  "colors": {
    "unhurt": 4294901860,
    "numeric": 4292018324,
    "dice": 4289374890,
    "direction": 4278255499,
    "fixture": 4287299584,
    "badly-hurt": 4278190335,
    "danger": 4278190335,
    "failure": 4278190335,
    "asleep": 4286611456,
    "wounded": 4278414273,
    "cash": 4294538159,
    "monster": 4278190335,
    "hidden": 4286093024,
    "scratched": 4280959314
  },
  "button-color": 4282599495,
  "text-color": 4288729810
}

```

### Library
```c
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <DrJson/drjson.h>

static
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
  const char* query = "foo.bar.baz";
  size_t qlen = strlen(query);
  DrJsonValue o = drjson_query(&v, query, qlen);
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

int
main(){
  const char* json =
    "{\n"
    "  foo:{\n"
    "    bar:{\n"
    "      baz:{\n"
    "        hello: 1,\n"
    "        world: 2,\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "}\n";
  int ret = write_foo_bar_baz_to_fp(json, strlen(json), stdout);
  putchar('\n');
  fflush(stdout);
  return ret;
}
```

It is recommended instead to provide your own allocator that provides an
efficient `free_all` function instead of having to deal with the
`drjson_slow_rescurive_free_all`.
See [`Demo/arena_allocator.h`](Demo/arena_allocator.h) for an example.

## Objects
Note that these objects are backed by hash tables that don't hate you, so the
order of insertion is preserved.  For duplicate keys, the last value seen is
used.

To avoid json key attacks, where a validator in one part disagrees about
ambiguities such as which key wins in the JSON spec, always serialize back from
the parsed json tree - don't reuse the original string. Also, drjson accepts
some extensions, so you especially want to serialize back to standard json!

## Extensions From JSON

Drjson can parse all valid json documents, but can also parse some documents not allowed by the spec.

1. Bare identifiers that are not `false`, `true` or `null` are treated
   as strings. For object keys, unsigned integers are treated as strings.
2. Hex literals (`0xf00dface`) are parsed as numbers.
3. css-style `#` colors are parsed as numbers. These can either be in the form
   of `#rgb`, `#rgba`, `#rrggbb`, or `#rrggbbaa`. They are stored as an unsigned
   integer in `abgr` order (`r` is the least significant byte).
4. Colons (`:`) and commas (`,`) are optional and treated as whitespace.
5. `//`-style comments causes everything until a newline to be treated as whitespace.
6. `/*`-style comments causes everything until a `*/` to be treated as whitespace.

When using these extensions, prefer `.drjson` as the file extension to avoid
confusion with regular json. The `drjson` cli tool can convert to normal json.
DrJson also provides `drjson_parse_braceless_object`, which is convenient when
using it as a config format.

## Number Handling

Numbers are parsed in a data-preserving way. DrJson has separate
types for Number (`double`), Integer (`int64_t`) and UInteger `uint64_t`.
Numbers in json docments without a decimal point or exponent are treated as
integers. Integers without a `-` sign are treated as unsigned integers.
Additionally, numbers that don't fit into their type will parse as strings
instead.

Be aware of this for two reasons:

1.  If you wanted "any number" you need to handle all 3 types and for "any
    integer" you need to handle both signed and unsigned 64 bit integers.

2.  DrJson will parse some numbers differently than other json parsers that
    always convert to double, such as the number 18014398509481985 (`2**54+1`).
    If stored as a double the 5 at the end will turn into a 4. As DrJson will
    parse this as unsigned 64 bit integer so the trailing digit will be
    preserved.

    On the other hand, 36893488147419103232 (`2 ** 65`) doesn't fit in a 64 bit
    integer, but a double can hold its value exactly. DrJson instead parses it
    as a string, which is maybe counter-intuitive.

## Future Directions

I'm not happy with the C API and would prefer to hand out opaque handles
instead of having objects and arrays inside a DrJsonValue.  This would allow
you to drop the "Boxed" option. Usage of the handles would need to be coupled
with a json context, would be an overall safer pattern. It would also allow
useful capabilities, such as easily querying all of the objects and
arrays from a json document without traversing the tree.
