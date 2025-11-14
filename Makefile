Bin: ; mkdir $@
Deps: ; mkdir $@
Fuzz: ; mkdir $@
DEBUG=-g
OPT=-O3

DRJSONVERSION=3.3.0

DEPFILES:= $(wildcard Deps/*.dep)
include $(DEPFILES)


.PHONY: clean
ifeq ($(OS),Windows_NT)
CC=clang
DYLIB=dll
DYLINK=lib
EXE=.exe
Bin/libdrjson.$(DRJSONVERSION).dll: DrJson/drjson.c | Bin Deps
	clang $< $(OPT) $(DEBUG) -o $@ -MT $@ -MD -MP -MF Deps/drjson.dll.dep -shared
clean: | Bin TestResults
	del /q Bin\* TestResults\*
else
UNAME := $(shell uname)
clean:
	rm -rf Bin/* TestResults/*

Bin/libdrjson.a: Bin/drjson.o | Bin
	ar crs $@ $^
all: Bin/libdrjson.a
ifeq ($(UNAME),Darwin)
DYLIB=dylib
DYLINK=dylib
EXE=
Bin/libdrjson.$(DRJSONVERSION).dylib: DrJson/drjson.c | Bin Deps
	$(CC) $< $(OPT) $(DEBUG) -o $@ -MT $@ -MD -MP -MF Deps/drjson.dylib.dep  -Wl,-headerpad_max_install_names -shared -install_name @executable_path/libdrjson.$(DRJSONVERSION).dylib -compatibility_version $(DRJSONVERSION) -current_version $(DRJSONVERSION) -arch arm64 -arch x86_64
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
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/test.dep Bin/libdrjson.$(DRJSONVERSION).$(DYLINK) -fvisibility=hidden -I. -g

Bin/test_static$(EXE): DrJson/test_drjson.c | Bin Deps
	$(CC) $< DrJson/drjson.c -o $@ -MT $@ -MD -MP -MF Deps/test_static.dep -fvisibility=hidden -I. -g -DDRJSON_STATIC_LIB=1

Bin/test_unity$(EXE): DrJson/test_drjson.c | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/test_unity.dep -fvisibility=hidden -I. -g -DDRJSON_UNITY=1

Bin/test_tui$(EXE): DrJson/test_drjson_tui.c | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/test_tui.dep -fvisibility=hidden -I. -g

Bin/test_tui_san$(EXE): DrJson/test_drjson_tui.c | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/test_tui.dep -fvisibility=hidden -I. -g -fsanitize=address,undefined

README.html: README.md README.css
	pandoc README.md README.css -f markdown -o $@ -s --toc

Bin/drjson$(EXE): DrJson/drjson_cli.c | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/drjson_cli.dep $(OPT) $(DEBUG) -fvisibility=hidden -I.

Bin/drj$(EXE): DrJson/drjson_tui.c | Bin Deps
	$(CC) $< -o $@ -MT $@ -MD -MP -MF Deps/drjson_tui.dep $(OPT) $(DEBUG) -fvisibility=hidden -I.


Bin/drjson_fuzz$(EXE): DrJson/drjson_fuzz.c | Bin Deps
	clang -O0 -g $< -o $@ -MT $@ -MD -MP -MF Deps/drjson_fuzz.dep -fsanitize=fuzzer,address,undefined

.PHONY: do-fuzz
do-fuzz: Bin/drjson_fuzz | Fuzz Deps
	$< Fuzz -fork=4

.PHONY: all
all: Bin/libdrjson.$(DRJSONVERSION).$(DYLIB)
all: Bin/drjson$(EXE)
all: Bin/drj$(EXE)
all: Bin/drjson.o
all: Bin/demo$(EXE)
all: Bin/test$(EXE)
all: Bin/test_static$(EXE)
all: Bin/test_unity$(EXE)
all: Bin/test_tui$(EXE)
all: Bin/test_tui_san$(EXE)

TestResults/%: Bin/%$(EXE) | TestResults
	$< --tee $@
TestResults: ; mkdir -p $@
.PHONY: tests
tests: TestResults/test
tests: TestResults/test_static
tests: TestResults/test_unity
tests: TestResults/test_tui
tests: TestResults/test_tui_san


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

ifneq ($(OS),Windows_NT) # shells out to unix commands
# lists makefile targets
.PHONY: list
list:
	@LC_ALL=C $(MAKE) -npRrq : 2>/dev/null \
		| awk -v RS= -F: '{if ($$1 !~ "^[#.]") {print $$1}}' \
		| sort \
		| uniq \
		| egrep -v \
			-e '^[^[:alnum:]]' \
			-e '^$@$$' \
			-e '.(dep|[ch])$$' \
			-e '^(Bin|Deps)' \
			-e '^(TestResults|Fuzz)' \
			-e '^(ArchivedWheels|wheelhouse)'

endif
include $(wildcard gather.mak)
