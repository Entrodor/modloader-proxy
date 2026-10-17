.RECIPEPREFIX = >
PYTHON ?= python3
GEN := build/gen
SRC := src/main.cpp src/exports.cpp
DEPS := src/proxy_exports.h $(GEN)/proxy_lists_gen.h
COMMON := -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ -s \
          -Isrc -I$(GEN) -Wl,--dynamicbase,--nxcompat,--no-insert-timestamp

.PHONY: all x64 x86 gen clean
all: x64 x86
gen: $(GEN)/proxy_lists_gen.h
x64: build/x64/modloader-proxy.dll
x86: build/x86/modloader-proxy.dll

# Regenerated whenever an exports/*.txt file is added or changed.
$(GEN)/proxy_lists_gen.h: $(wildcard exports/*.txt) tools/gen_exports.py
> $(PYTHON) tools/gen_exports.py exports $(GEN)

build/x64/modloader-proxy.dll: $(SRC) $(DEPS)
> mkdir -p $(@D)
> x86_64-w64-mingw32-g++ $(COMMON) -o $@ $(SRC)

build/x86/modloader-proxy.dll: $(SRC) $(DEPS)
> mkdir -p $(@D)
> i686-w64-mingw32-g++ $(COMMON) -o $@ $(SRC)

clean:
> rm -rf build dist
