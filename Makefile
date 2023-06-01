Bin: ; mkdir $@
Deps: ; mkdir $@
Fuzz: ; mkdir $@
DEBUG=-g
OPT=-O3

DRJSONVERSION=2.0.0

DEPFILES:= $(wildcard Deps/*.dep)
include $(DEPFILES)


ifeq ($(OS),Windows_NT)
CC=clang
DYLIB=dll
DYLINK=lib
EXE=.exe
Bin/libdrjson.$(DRJSONVERSION).dll: DrJson/drjson.c | Bin Deps
	clang $< $(OPT) $(DEBUG) -o $@ -MT $@ -MD -MP -MF Deps/drjson.dll.dep -shared
clean:
	del /q Bin\*
else
UNAME := $(shell uname)
clean:
	rm -rf Bin/*

Bin/libdrjson.a: Bin/drjson.o | Bin
	ar crs $@ $^
all: Bin/libdrjson.a
ifeq ($(UNAME),Darwin)
DYLIB=dylib
DYLINK=dylib
EXE=
Bin/libdrjson.$(DRJSONVERSION).dylib: DrJson/drjson.c | Bin Deps
	$(CC) $< $(OPT) $(DEBUG) -o $@ -MT $@ -MD -MP -MF Deps/drjson.dylib.dep  -Wl,-headerpad_max_install_names -Wl,-undefined,error -shared -install_name @executable_path/libdrjson.$(DRJSONVERSION).dylib -compatibility_version $(DRJSONVERSION) -current_version $(DRJSONVERSION) -arch arm64 -arch x86_64
else
DYLIB=so
DYLINK=so
EXE=
Bin/libdrjson.$(DRJSONVERSION).so: DrJson/drjson.c | Bin Deps
	$(CC) $< $(OPT) $(DEBUG) -o $@ -MT $@ -MD -MP -MF Deps/drjson.so.dep -shared
endif
endif

Bin/drjson.o: DrJson/drjson.c | Bin Deps
	$(CC) -c $< -o $@ -MT $@ -MD -MP -MF Deps/drjson.dep  $(OPT) $(DEBUG)

Bin/demo$(EXE): Demo/demo.c Bin/libdrjson.$(DRJSONVERSION).$(DYLIB) | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/demo.dep $(OPT) $(DEBUG) Bin/libdrjson.$(DRJSONVERSION).$(DYLINK) -fvisibility=hidden -I.

Bin/test$(EXE): DrJson/test_drjson.c Bin/libdrjson.$(DRJSONVERSION).$(DYLIB) | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/test.dep Bin/libdrjson.$(DRJSONVERSION).$(DYLINK) -fvisibility=hidden -I.


README.html: README.md README.css
	pandoc README.md README.css -f markdown -o $@ -s --toc

Bin/drjson$(EXE): DrJson/drjson_cli.c | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/drjson_cli.dep $(OPT) $(DEBUG) -fvisibility=hidden -I.
.PHONY: clean


Bin/drjson_fuzz$(EXE): DrJson/drjson_fuzz.c | Bin Deps
	clang -O0 -g $< -o $@ -MT $@ -MD -MP -MF Deps/drjson_fuzz.dep -fsanitize=fuzzer,address,undefined

.PHONY: do-fuzz
do-fuzz: Bin/drjson_fuzz | Fuzz Deps
	$< Fuzz -fork=4

.PHONY: all
all: Bin/libdrjson.$(DRJSONVERSION).$(DYLIB)
all: Bin/drjson$(EXE)
all: Bin/drjson.o
all: Bin/demo$(EXE)
all: Bin/test$(EXE)

.DEFAULT_GOAL:=all

ifeq ($(OS),Windows_NT)
UNAME:=Windows
else
UNAME := $(shell uname)
endif

.PHONY: drjson-wheel
.PHONY: wheels
ifeq ($(UNAME),Darwin)
civenv:
	python3 -m venv civenv
	. civenv/bin/activate && python -m pip install cibuildwheel && python -m pip install twine

# macos you need to build multiple times
wheels: civenv
	rm -rf dist build
	rm -f wheelhouse/*.whl
	. civenv/bin/activate && CIBW_SKIP='{pp*,cp36*,cp37*}' cibuildwheel --platform macos --archs x86_64 .
	. civenv/bin/activate && CIBW_SKIP='{pp*,cp36*,cp37*}' cibuildwheel --platform macos --archs arm64 .
	. civenv/bin/activate && CIBW_SKIP='{pp*,cp36*,cp37*}' cibuildwheel --platform macos --archs universal2 .
	rm -rf PyDrJson/drjson.egg-info

endif

ifeq ($(UNAME),Linux)
civenv:
	python3 -m venv civenv
	. civenv/bin/activate && python -m pip install cibuildwheel && python -m pip install twine

wheels: civenv
	rm -rf dist build
	rm -f wheelhouse/*.whl
	. civenv/bin/activate && CIBW_SKIP='{pp*,*musl*}' cibuildwheel --platform linux --archs x86_64 .

endif

ifeq ($(UNAME),Windows)
civenv:
	py -m venv civenv
	civenv\Scripts\activate && py -m pip install cibuildwheel && py -m pip install twine

wheels: civenv
	-rmdir dist build wheelhouse /s /q
	civenv\Scripts\activate && cmd /V /C "SET CIBW_SKIP={pp*,cp36*,cp37*} && cibuildwheel --platform windows --archs AMD64 ."
endif


.PHONY: pypi-upload
pypi-upload: archive-wheels civenv
	. civenv/bin/activate && python3 -m twine upload wheelhouse/* --verbose

.PHONY: archive-wheels
archive-wheels: | ArchivedWheels
	cp wheelhouse/*.whl ArchivedWheels
ArchivedWheels: ; mkdir -p $@

include $(wildcard gather.mak)
