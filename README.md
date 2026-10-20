# modloader-proxy

> This Project is made with the help of Claude. Expect bugs or bad coding practices.
> If you don't like Vibecoded Stuff, then please find something else.

A small, configurable DLL proxy / chainloader for **singleplayer game mods** that
all need the same kind of loader. Rename the DLL to something the game already
loads, list your mod DLLs in `chainloader.ini` (and/or drop them in a folder), and
they get loaded at startup. The original system DLL's exports are forwarded, so
the game keeps working.

> Intended for singleplayer and private/dedicated-server modding. Do not use it
> with games protected by anti-cheat or in public multiplayer.

## Install

1. Download the zip matching the game's bitness (`x64` or `x86`) from Releases.
2. Rename `modloader-proxy.dll` to a supported name the game imports
   (see `supported_dlls.txt` in the zip).
3. Put it in the folder containing the game's `.exe`, next to `chainloader.ini`.
4. Add your mods: list them under `[Load]`, and/or enable folder loading.

**Finding a name the game uses:** check the exe's imports with
`objdump -p Game.exe | grep "DLL Name"` (Linux/MSYS2), `dumpbin /imports`, or PE-bear.
Headless dedicated servers often don't load `dinput8`/`dsound` at all, so prefer
`version.dll` or similar there.

## chainloader.ini

| Setting | Default | Meaning |
|---|---|---|
| `Log` | `false` | Write `modloader-proxy.log` next to the DLL |
| `Deferred` | `false` | Load mods from a separate thread instead of inside `DllMain` |
| `DelayMs` | `0` | Wait before loading (only with `Deferred = true`) |
| `LoadFromFolder` | `false` | Load every `*.dll` in `ModsFolder` |
| `ModsFolder` | `mods` | Subfolder to scan (relative to the ini, not recursive) |

`[Load]` takes one DLL path per line. Load order: `[Load]` entries first, in order,
then folder DLLs alphabetically. A DLL listed in both is only loaded once.

## Chaining another proxy mod (`ForwardTo`)

Some mods are proxies themselves (their own `dinput8.dll`, say) and rely on the
*game* calling their exports. Loading them as a normal mod runs their `DllMain`,
but the game never calls them, because it is linked to this proxy. Set:

```ini
[Settings]
ForwardTo = mods\XHL\dinput8.dll
```

The call path becomes game -> this proxy -> that mod -> real system DLL. For each
exported function, the mod's export of the same name is used if it has one,
otherwise the call goes straight to the system DLL. The log reports how many
exports were routed to the mod.

Notes:
- One target only: a call can't be split between several mods. Additional proxy mods
  would have to chain to each other themselves.
- If the mod finds the real DLL from its own file name, give it the name of the
  proxy you chose, in its own folder (as in the example above).
- It is loaded before the game can call anything, and is not loaded a second time
  as a normal mod.

## Adding a new proxy name (no C++ changes)

Export tables are fixed when a DLL is linked, so one prebuilt binary can only
proxy names it was built with. Instead of hardcoding them, each proxy target is a
plain text file in `exports/` (`exports/dbghelp.txt` -> `dbghelp.dll`, one export
name per line). To add one:

1. On a Windows machine with Python 3, dump the real DLL's exports (pass both the
   64-bit and 32-bit copies if you can; the union is safe for both):
   ```
   python tools/dump_exports.py -o exports/dbghelp.txt C:\Windows\System32\dbghelp.dll C:\Windows\SysWOW64\dbghelp.dll
   ```
2. Commit and push (or tag a release). CI regenerates the stubs and rebuilds.

Limitations: ordinal-only exports and C++-mangled export names can't be proxied
and are skipped with a warning.

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
Locally (Linux/WSL/MSYS2 with `mingw-w64` and Python 3): `make all`.

## How it works

Every exported name is a bare `jmp *pointer` stub; at load, the proxy works out
its own filename, loads the real DLL from the system directory, and fills in the
pointers. Because it never touches arguments, one binary works for any
calling convention. Then it loads the DLLs from `chainloader.ini` and the mods folder.
