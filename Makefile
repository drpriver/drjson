Bin: ; mkdir -p $@
Deps: ; mkdir -p $@
Fuzz: ; mkdir -p $@
DEBUG=
OPT=-O3

DRJSONVERSION=1.0.0

DEPFILES:= $(wildcard Deps/*.dep)
include $(DEPFILES)

Bin/libdrjson.a: Bin/drjson.o | Bin
	ar crs $@ $^

Bin/libdrjson.$(DRJSONVERSION).dylib: DrJson/drjson.c | Bin Deps
	$(CC) $< $(OPT) $(DEBUG) -o $@ -MT $@ -MD -MP -MF Deps/drjson.dylib.dep  -Wl,-headerpad_max_install_names -Wl,-undefined,error -shared -install_name @executable_path/libdrjson.$(DRJSONVERSION).dylib -compatibility_version $(DRJSONVERSION) -current_version $(DRJSONVERSION)

Bin/drjson.o: DrJson/drjson.c | Bin Deps
	$(CC) -c $< -o $@ -MT $@ -MD -MP -MF Deps/drjson.dep  $(OPT) $(DEBUG)

Bin/demo: Demo/demo.c Bin/libdrjson.$(DRJSONVERSION).dylib
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep $(OPT) $(DEBUG) Bin/libdrjson.$(DRJSONVERSION).dylib -fvisibility=hidden -I.


README.html: README.md README.css
	pandoc README.md README.css -f markdown -o $@ -s --toc

Bin/drjson: DrJson/drjson_cli.c Bin/libdrjson.$(DRJSONVERSION).dylib
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep $(OPT) $(DEBUG) Bin/libdrjson.$(DRJSONVERSION).dylib -fvisibility=hidden -I.
.PHONY: clean
clean:
	rm -rf Bin/*


Bin/drjson_fuzz: DrJson/drjson_fuzz.c
	clang -O0 -g $< -o $@ -MT $@ -MD -MP -MF Deps/drjson_fuzz.dep -fsanitize=fuzzer,address,undefined

.PHONY: fuzz
fuzz: Bin/drjson_fuzz | Fuzz
	$< Fuzz -fork=4

.PHONY: all
all: Bin/libdrjson.a Bin/libdrjson.$(DRJSONVERSION).dylib Bin/drjson Bin/drjson.o Bin/demo

.DEFAULT_GOAL:=all
