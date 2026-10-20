// modloader-proxy: configurable DLL proxy / chainloader for singleplayer game mods.
// Rename modloader-proxy.dll to a DLL name the game imports (e.g. version.dll),
// list the DLLs to load in chainloader.ini, done.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#include "proxy_exports.h"

namespace {

struct Config {
    bool log = false;
    bool deferred = false;
    DWORD delayMs = 0;
    bool loadFolder = false;
    std::wstring folder = L"mods";
    std::wstring forwardTo;  // optional proxy-mod DLL that receives the game's calls
    std::vector<std::wstring> mods;
};

std::wstring g_dir;       // directory of this DLL, with trailing backslash
std::wstring g_selfName;  // lower-case file name of this DLL
bool g_log = false;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return {};
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

void Log(const wchar_t* fmt, ...) {
    if (!g_log) return;
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    std::string line = Narrow(buf) + "\r\n";
    HANDLE h = CreateFileW((g_dir + L"modloader-proxy.log").c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, line.data(), (DWORD)line.size(), &written, nullptr);
    CloseHandle(h);
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1 << 20)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    out.resize(got);
    return ok != FALSE;
}

bool ParseBool(const std::wstring& v) {
    return v == L"1" || v == L"true" || v == L"yes" || v == L"on";
}

// Minimal INI parser: [Settings] has key=value, [Load] has one DLL path per line.
bool LoadConfig(Config& cfg) {
    std::string raw;
    if (!ReadWholeFile(g_dir + L"chainloader.ini", raw)) return false;
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
        (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);
    std::wstring text = Widen(raw), section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = Trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = Lower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t sc = line.find(L" ;");  // inline comment
        if (sc != std::wstring::npos) line = Trim(line.substr(0, sc));
        if (line.empty()) continue;

        if (section == L"settings") {
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = Lower(Trim(line.substr(0, eq)));
            std::wstring value = Trim(line.substr(eq + 1));  // case preserved (folder names)
            std::wstring val = Lower(value);
            if (key == L"log") cfg.log = ParseBool(val);
            else if (key == L"deferred") cfg.deferred = ParseBool(val);
            else if (key == L"delayms") cfg.delayMs = (DWORD)_wtoi(val.c_str());
            else if (key == L"loadfromfolder") cfg.loadFolder = ParseBool(val);
            else if (key == L"modsfolder") cfg.folder = value;
            else if (key == L"forwardto") cfg.forwardTo = value;
        } else if (section == L"load") {
            cfg.mods.push_back(line);
        }
    }
    return true;
}

// ---- real DLL forwarding ------------------------------------------------

struct ProxyTarget {
    const wchar_t* dll;
    void (*bind)(HMODULE real, HMODULE fwd);
};

// Each export prefers the ForwardTo module's export of the same name (if there is one),
// and falls back to the real system DLL's.
int g_forwarded = 0;  // how many exports were routed to the ForwardTo module
#define BIND(n)                                                          \
    fp_##n = fwd ? GetProcAddress(fwd, #n) : nullptr;                    \
    if (fp_##n) ++g_forwarded;                                           \
    else fp_##n = real ? GetProcAddress(real, #n) : nullptr;
#include "proxy_targets_gen.inc"  // generated: Bind_N() functions + kTargets[]

void BindRealDll(HMODULE fwd) {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { Log(L"GetSystemDirectory failed"); return; }
    for (const auto& t : kTargets) {
        if (g_selfName != t.dll) continue;
        std::wstring p = std::wstring(sys) + L"\\" + t.dll;
        HMODULE real = LoadLibraryW(p.c_str());
        if (!real) Log(L"Could not load real %ls (error %lu)", p.c_str(), GetLastError());
        g_forwarded = 0;
        t.bind(real, fwd);
        Log(L"Forwarding exports to %ls", p.c_str());
        if (fwd) {
            if (g_forwarded) Log(L"%d export(s) routed to the ForwardTo module first", g_forwarded);
            else Log(L"WARNING: the ForwardTo module exports none of this proxy's functions");
        }
        return;
    }
    Log(L"'%ls' is not a supported proxy name; exports will not be forwarded", g_selfName.c_str());
}

// ---- chainloading -------------------------------------------------------

std::wstring Resolve(std::wstring p) {
    for (auto& c : p) if (c == L'/') c = L'\\';
    bool absolute = (p.size() > 1 && p[1] == L':') || (p.size() > 1 && p[0] == L'\\' && p[1] == L'\\');
    return absolute ? p : g_dir + p;
}

// Non-recursive scan for *.dll, sorted by name (case-insensitive).
void ScanFolder(std::wstring dir, std::vector<std::wstring>& out) {
    if (dir.empty() || dir.back() != L'\\') dir += L'\\';
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        Log(L"No DLLs found in folder %ls (error %lu)", dir.c_str(), GetLastError());
        return;
    }
    std::vector<std::wstring> found;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        // "*.dll" can also match longer extensions via 8.3 short names; require exactly .dll
        if (name.size() < 4 || Lower(name.substr(name.size() - 4)) != L".dll") continue;
        found.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(found.begin(), found.end(),
              [](const std::wstring& a, const std::wstring& b) { return Lower(a) < Lower(b); });
    for (const auto& n : found) out.push_back(dir + n);
}

void LoadMods(const Config& cfg) {
    // Explicit [Load] entries first (in the order written), then the folder (alphabetical).
    std::vector<std::wstring> paths;
    for (const auto& e : cfg.mods) paths.push_back(Resolve(e));

    if (cfg.loadFolder) {
        std::wstring f = Trim(cfg.folder);
        for (auto& c : f) if (c == L'/') c = L'\\';
        while (!f.empty() && f.back() == L'\\') f.pop_back();
        if (f.empty() || f == L".") {
            Log(L"ModsFolder must be a subfolder, not the game folder itself; skipping folder load");
        } else {
            std::vector<std::wstring> scanned;
            ScanFolder(Resolve(f), scanned);
            for (const auto& p : scanned) {
                bool dup = false;
                for (const auto& q : paths) if (Lower(q) == Lower(p)) { dup = true; break; }
                if (!dup) paths.push_back(p);
            }
        }
    }

    std::wstring fwdPath = cfg.forwardTo.empty() ? std::wstring() : Lower(Resolve(cfg.forwardTo));
    for (const auto& path : paths) {
        size_t slash = path.find_last_of(L'\\');
        if (Lower(path.substr(slash + 1)) == g_selfName) continue;  // never load ourselves
        if (!fwdPath.empty() && Lower(path) == fwdPath) continue;   // already loaded as ForwardTo
        HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m) Log(L"Loaded %ls", path.c_str());
        else Log(L"FAILED to load %ls (error %lu)", path.c_str(), GetLastError());
    }
}

// Loads the ForwardTo module early (before the game can call our exports).
HMODULE LoadForwardTarget(const std::wstring& entry) {
    std::wstring path = Resolve(entry);
    if (Lower(path) == Lower(g_dir + g_selfName)) {
        Log(L"ForwardTo points at the proxy itself; ignoring");
        return nullptr;
    }
    HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m) Log(L"ForwardTo module loaded: %ls", path.c_str());
    else Log(L"FAILED to load ForwardTo module %ls (error %lu)", path.c_str(), GetLastError());
    return m;
}

DWORD WINAPI DeferredThread(LPVOID p) {
    std::unique_ptr<Config> cfg(static_cast<Config*>(p));
    if (cfg->delayMs) Sleep(cfg->delayMs);
    LoadMods(*cfg);
    return 0;
}

void Init(HINSTANCE self) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring full(path, n);
    size_t slash = full.find_last_of(L"\\/");
    g_dir = full.substr(0, slash + 1);
    g_selfName = Lower(full.substr(slash + 1));

    Config cfg;
    bool haveCfg = LoadConfig(cfg);
    g_log = cfg.log;
    if (!haveCfg) Log(L"chainloader.ini not found in %ls", g_dir.c_str());

    // First: the game may call our exports immediately.
    HMODULE fwd = cfg.forwardTo.empty() ? nullptr : LoadForwardTarget(cfg.forwardTo);
    BindRealDll(fwd);

    if (cfg.deferred) {
        auto* heap = new Config(cfg);
        HANDLE t = CreateThread(nullptr, 0, DeferredThread, heap, 0, nullptr);
        if (t) CloseHandle(t); else { delete heap; LoadMods(cfg); }
    } else {
        LoadMods(cfg);
    }
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init(inst);
    }
    return TRUE;
}
