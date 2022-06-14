# DrJson

DrJson is a liberal json parsing and querying library for C.
It provides functions that parse a json document as well as a simple query
language for retrieving values.  It also provides a json printer/pretty-printer.

## Building

A meson.build and a CMakeLists.txt are provided for building. Or you can use
the Makefile, but that is primarily for development.

## Usage

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

<style>
/*
Obtained from https://github.com/sindresorhus/github-markdown-css/blob/main/github-markdown.css

Modified a bit to cut out the bloat.
*/
@media (prefers-color-scheme: dark) {
  body {
    color-scheme: dark;
    --fg-default: #c9d1d9;
    --fg-muted: #8b949e;
    --fg-subtle: #484f58;
    --canvas-default: #0d1117;
    --canvas-subtle: #161b22;
    --border-default: #30363d;
    --border-muted: #21262d;
    --neutral-muted: rgba(110,118,129,0.4);
    --accent-fg: #58a6ff;
    --accent-emphasis: #1f6feb;
    --attention-subtle: rgba(187,128,9,0.15);
  }
}

@media (prefers-color-scheme: light) {
  body {
    color-scheme: light;
    --fg-default: #24292f;
    --fg-muted: #57606a;
    --fg-subtle: #6e7781;
    --canvas-default: #ffffff;
    --canvas-subtle: #f6f8fa;
    --border-default: #d0d7de;
    --border-muted: hsla(210,18%,87%,1);
    --neutral-muted: rgba(175,184,193,0.2);
    --accent-fg: #0969da;
    --accent-emphasis: #0969da;
    --attention-subtle: #fff8c5;
  }
}
body {
  -ms-text-size-adjust: 100%;
  -webkit-text-size-adjust: 100%;
  color: var(--fg-default);
  background-color: var(--canvas-default);
  font-family: -apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif,"Apple Color Emoji","Segoe UI Emoji";
  font-size: 16px;
  line-height: 1.5;
  word-wrap: break-word;
}
body md,
body figcaption,
body figure {
  display: block;
}
body summary {
  display: list-item;
}
body [hidden] {
  display: none !important;
}
body a {
  background-color: transparent;
  color: var(--accent-fg);
  text-decoration: none;
}
body a:active,
body a:hover {
  outline-width: 0;
}
body abbr[title] {
  border-bottom: none;
  text-decoration: underline dotted;
}
body b,
body strong {
  font-weight: 600;
}
body dfn {
  font-style: italic;
}
body h1 {
  margin: .67em 0;
  font-weight: 600;
  padding-bottom: .3em;
  font-size: 2em;
  border-bottom: 1px solid var(--border-muted);
}
body mark {
  background-color: var(--attention-subtle);
  color: var(--text-primary);
}
body small {
  font-size: 90%;
}
body sub,
body sup {
  font-size: 75%;
  line-height: 0;
  position: relative;
  vertical-align: baseline;
}
body sub {
  bottom: -0.25em;
}
body sup {
  top: -0.5em;
}
body img {
  border-style: none;
  max-width: 100%;
  box-sizing: content-box;
  background-color: var(--canvas-default);
}
body code,
body kbd,
body pre,
body samp {
  font-family: monospace,monospace;
  font-size: 1em;
}
body figure {
  margin: 1em 40px;
}
body hr {
  box-sizing: content-box;
  overflow: hidden;
  background: transparent;
  border-bottom: 1px solid var(--border-muted);
  height: .25em;
  padding: 0;
  margin: 24px 0;
  background-color: var(--border-default);
  border: 0;
}
body input {
  font: inherit;
  margin: 0;
  overflow: visible;
  font-family: inherit;
  font-size: inherit;
  line-height: inherit;
}
body a:hover {
  text-decoration: underline;
}
body hr::before {
  display: table;
  content: "";
}
body hr::after {
  display: table;
  clear: both;
  content: "";
}
body table {
  border-spacing: 0;
  border-collapse: collapse;
  display: block;
  width: max-content;
  max-width: 100%;
  overflow: auto;
}
body td,
body th {
  padding: 0;
}
body h1,
body h2,
body h3,
body h4,
body h5,
body h6 {
  margin-top: 24px;
  margin-bottom: 16px;
  font-weight: 600;
  line-height: 1.25;
}
body h2 {
  font-weight: 600;
  padding-bottom: .3em;
  font-size: 1.5em;
  border-bottom: 1px solid var(--border-muted);
}
body h3 {
  font-weight: 600;
  font-size: 1.25em;
}
body h4 {
  font-weight: 600;
  font-size: 1em;
}
body h5 {
  font-weight: 600;
  font-size: .875em;
}
body h6 {
  font-weight: 600;
  font-size: .85em;
  color: var(--fg-muted);
}
body p {
  margin-top: 0;
  margin-bottom: 10px;
}
body blockquote {
  margin: 0;
  padding: 0 1em;
  color: var(--fg-muted);
  border-left: .25em solid var(--border-default);
}
body ul,
body ol {
  margin-top: 0;
  margin-bottom: 0;
  padding-left: 2em;
}
body ol ol,
body ul ol {
  list-style-type: lower-roman;
}
body ul ul ol,
body ul ol ol,
body ol ul ol,
body ol ol ol {
  list-style-type: lower-alpha;
}
body dd {
  margin-left: 0;
}
body tt,
body code {
  font-family: ui-monospace,SFMono-Regular,SF Mono,Menlo,Consolas,Liberation Mono,monospace;
  font-size: 12px;
}
body pre {
  margin-top: 0;
  margin-bottom: 0;
  font-family: ui-monospace,SFMono-Regular,SF Mono,Menlo,Consolas,Liberation Mono,monospace;
  font-size: 12px;
  word-wrap: normal;
}
body ::placeholder {
  color: var(--fg-subtle);
  opacity: 1;
}
body>*:first-child {
  margin-top: 0 !important;
}
body>*:last-child {
  /*margin-bottom: 0 !important;*/
}
body a:not([href]) {
  color: inherit;
  text-decoration: none;
}
body p,
body blockquote,
body ul,
body ol,
body dl,
body table,
body pre,
body md {
  margin-top: 0;
  margin-bottom: 16px;
}
body blockquote>:first-child {
  margin-top: 0;
}
body blockquote>:last-child {
  margin-bottom: 0;
}
body sup>a::before {
  content: "[";
}
body sup>a::after {
  content: "]";
}
body h1 tt,
body h1 code,
body h2 tt,
body h2 code,
body h3 tt,
body h3 code,
body h4 tt,
body h4 code,
body h5 tt,
body h5 code,
body h6 tt,
body h6 code {
  padding: 0 .2em;
  font-size: inherit;
}
body ol[type="1"] {
  list-style-type: decimal;
}
body ol[type=a] {
  list-style-type: lower-alpha;
}
body ol[type=i] {
  list-style-type: lower-roman;
}
body div>ol:not([type]) {
  list-style-type: decimal;
}
body ul ul,
body ul ol,
body ol ol,
body ol ul {
  margin-top: 0;
  margin-bottom: 0;
}
body li>p {
  margin-top: 16px;
}
body li+li {
  margin-top: .25em;
}
body dl {
  padding: 0;
}
body dl dt {
  padding: 0;
  margin-top: 16px;
  font-size: 1em;
  font-style: italic;
  font-weight: 600;
}
body dl dd {
  padding: 0 16px;
  margin-bottom: 16px;
}
body table th {
  font-weight: 600;
}
body table th,
body table td {
  padding: 6px 13px;
  border: 1px solid var(--border-default);
}
body table tr {
  background-color: var(--canvas-default);
  border-top: 1px solid var(--border-muted);
}
body table tr:nth-child(2n) {
  background-color: var(--canvas-subtle);
}
body table img {
  background-color: transparent;
}
body img[align=right] {
  padding-left: 20px;
}
body img[align=left] {
  padding-right: 20px;
}
body code,
body tt {
  padding: .2em .4em;
  margin: 0;
  font-size: 85%;
  background-color: var(--neutral-muted);
  border-radius: 6px;
}
body code br,
body tt br {
  display: none;
}
body del code {
  text-decoration: inherit;
}
body pre code {
  font-size: 100%;
}
body pre>code {
  padding: 0;
  margin: 0;
  word-break: normal;
  white-space: pre;
  background: transparent;
  border: 0;
}
body pre {
  padding: 16px;
  overflow: auto;
  font-size: 85%;
  line-height: 1.45;
  background-color: var(--canvas-subtle);
  border-radius: 6px;
}
body pre code,
body pre tt {
  display: inline;
  max-width: auto;
  padding: 0;
  margin: 0;
  overflow: visible;
  line-height: inherit;
  word-wrap: normal;
  background-color: transparent;
  border: 0;
}
body ::-webkit-calendar-picker-indicator {
  filter: invert(50%);
}
body {
  width: 40em;
  margin: auto;
  margin-bottom: 24ex;
}
#TOC {
  position: fixed;
  left: 0px;
  top: 0px;
}
</style>
