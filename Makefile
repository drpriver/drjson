Bin: ; mkdir -p $@
Deps: ; mkdir -p $@

DRJSONVERSION=1.0.0

DEPFILES:= $(wildcard Deps/*.dep)
include $(DEPFILES)

Bin/libdrjson.a: Bin/drjson.o | Bin
	ar crs $@ $^

Bin/libdrjson.$(DRJSONVERSION).dylib: DrJson/drjson.c | Bin Deps
	$(CC) $< -O3 -o $@ -MT $@ -MD -MP -MF Deps/drjson.dylib.dep  -Wl,-headerpad_max_install_names -Wl,-undefined,error -shared -install_name @executable_path/libdrjson.$(DRJSONVERSION).dylib -compatibility_version $(DRJSONVERSION) -current_version $(DRJSONVERSION) -g

Bin/drjson.o: DrJson/drjson.c | Bin Deps
	$(CC) -c $< -o $@ -MT $@ -MD -MP -MF Deps/drjson.dep  -O3

Bin/demo: Demo/demo.c Bin/libdrjson.$(DRJSONVERSION).dylib
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep -O1 -g Bin/libdrjson.$(DRJSONVERSION).dylib -fvisibility=hidden -I.

.DEFAULT_GOAL:=Bin/drjson

README.html: README.md
	pandoc $< -o $@ -s --toc

Bin/drjson: DrJson/drjson_cli.c Bin/libdrjson.$(DRJSONVERSION).dylib
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep -O1 -g Bin/libdrjson.$(DRJSONVERSION).dylib -fvisibility=hidden -I.
