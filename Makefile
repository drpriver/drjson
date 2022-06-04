Bin: ; mkdir -p $@
Deps: ; mkdir -p $@

DEPFILES:= $(wildcard Deps/*.dep)
include $(DEPFILES)

Bin/libdrjson.a: Bin/drjson.o | Bin
	ar crs $@ $^

Bin/drjson.o: drjson.c | Bin Deps
	$(CC) -c $< -o $@ -MT $@ -MD -MP -MF Deps/drjson.dep  -O3
Bin/demo: demo.c Bin/drjson.o
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep -O1 -g

.DEFAULT_GOAL:=Bin/demo
