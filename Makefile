Bin: ; mkdir -p $@
Deps: ; mkdir -p $@

DRJSONVERSION=0.1.0

DEPFILES:= $(wildcard Deps/*.dep)
include $(DEPFILES)

Bin/libdrjson.a: Bin/drjson.o | Bin
	ar crs $@ $^

Bin/libdrjson.$(DRJSONVERSION).dylib: drjson.c | Bin Deps
	$(CC) $< -O1 -o $@ -MT $@ -MD -MP -MF Deps/drjson.dylib.dep  -Wl,-headerpad_max_install_names -Wl,-undefined,error -shared -install_name @executable_path/libdrjson.$(DRJSONVERSION).dylib -compatibility_version $(DRJSONVERSION) -current_version $(DRJSONVERSION) -g

Bin/drjson.o: drjson.c | Bin Deps
	$(CC) -c $< -o $@ -MT $@ -MD -MP -MF Deps/drjson.dep  -O3
Bin/demo: demo.c Bin/libdrjson.$(DRJSONVERSION).dylib
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep -O1 -g Bin/libdrjson.$(DRJSONVERSION).dylib -fvisibility=hidden

.DEFAULT_GOAL:=Bin/demo
