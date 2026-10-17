# modloader-proxy

A small, configurable DLL proxy / chainloader for **singleplayer game mods** that
all need the same kind of loader. Rename the DLL to something the game already
loads, list your mod DLLs in `chainloader.ini`, and they get loaded at startup.
The original system DLL's exports are forwarded, so the game keeps working.

> Intended for singleplayer and private/dedicated-server modding. Do not use it
> with games protected by anti-cheat or in public multiplayer.

## Install

1. Download the zip matching the game's bitness (`x64` or `x86`) from Releases.
2. Rename `modloader-proxy.dll` to a supported name the game imports:
   `version.dll`, `dinput8.dll`, `dsound.dll`
3. Put it in the folder containing the game's `.exe`, next to `chainloader.ini`.
4. Edit `chainloader.ini` and list your mod DLLs under `[Load]`.

**Finding a name the game uses:** check the exe's imports with
`objdump -p Game.exe | grep "DLL Name"` (Linux/MSYS2), `dumpbin /imports`, or PE-bear.
`version.dll` is a common choice. Headless dedicated servers often don't load
`dinput8`/`dsound` at all, so prefer `version.dll` there.

## chainloader.ini

See the commented file in this repo. `[Load]` takes one DLL path per line
(relative to the ini). `[Settings]` has `Log`, `Deferred`, `DelayMs`.
With `Log = true`, a `modloader-proxy.log` is written next to the DLL.

## Linux / Proton / Wine (including dedicated servers)

Wine only loads a local DLL over its builtin one if told to. Use
`WINEDLLOVERRIDES` with the name you chose (`n,b` = native first, then builtin):

**Steam / Proton** (Launch Options):
```
WINEDLLOVERRIDES="version=n,b" %command%
```

**Dedicated server under Wine:**
```
WINEDLLOVERRIDES="version=n,b" wine ./Server.exe
```
For headless servers you can also silence unneeded components:
`WINEDLLOVERRIDES="version=n,b;mscoree=;mshtml="`

**Persistent alternative** (stored in the prefix):
```
wine reg add "HKCU\Software\Wine\DllOverrides" /v version /d native,builtin /f
```
Use the same DLL name in the override as the file you renamed to.

## Building

CI builds both architectures with MinGW-w64 and attaches zips to a Release
whenever you push a tag like `v0.1.0`:
```
git tag v0.1.0 && git push --tags
```
Locally (Linux/WSL/MSYS2 with `mingw-w64` installed): `make all`.

## Adding another proxy name

Edit `src/proxy_exports.h` (add the export list), then add a `Bind*` function
and `kTargets` entry in `src/main.cpp`. Names shared with an existing DLL
(e.g. `DllGetClassObject`) must only appear once in `LIST_ALL`.

## How it works

Every exported name is a bare `jmp *pointer` stub; at load, the proxy works out
its own filename, loads the real DLL from the system directory, and fills in the
pointers. Because it never touches arguments, one binary works for any
calling convention. Then it loads each DLL from `chainloader.ini`.
