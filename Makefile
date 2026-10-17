.RECIPEPREFIX = >
SRC := src/main.cpp src/exports.cpp
COMMON := -std=c++17 -O2 -Wall -Wextra -shared -static -static-libgcc -static-libstdc++ -s \
          -Wl,--dynamicbase,--nxcompat,--no-insert-timestamp

.PHONY: all x64 x86 clean
all: x64 x86
x64: build/x64/modloader-proxy.dll
x86: build/x86/modloader-proxy.dll

build/x64/modloader-proxy.dll: $(SRC) src/proxy_exports.h
> mkdir -p $(@D)
> x86_64-w64-mingw32-g++ $(COMMON) -o $@ $(SRC)

build/x86/modloader-proxy.dll: $(SRC) src/proxy_exports.h
> mkdir -p $(@D)
> i686-w64-mingw32-g++ $(COMMON) -o $@ $(SRC)

clean:
> rm -rf build dist
