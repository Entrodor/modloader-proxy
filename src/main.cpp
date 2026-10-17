// modloader-proxy: configurable DLL proxy / chainloader for singleplayer game mods.
// Rename modloader-proxy.dll to a DLL name the game imports (e.g. version.dll),
// list the DLLs to load in chainloader.ini, done.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
        if (section == L"settings") {
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = Lower(Trim(line.substr(0, eq)));
            std::wstring val = Lower(Trim(line.substr(eq + 1)));
            if (key == L"log") cfg.log = ParseBool(val);
            else if (key == L"deferred") cfg.deferred = ParseBool(val);
            else if (key == L"delayms") cfg.delayMs = (DWORD)_wtoi(val.c_str());
        } else if (section == L"load") {
            size_t sc = line.find(L" ;");  // inline comment
            if (sc != std::wstring::npos) line = Trim(line.substr(0, sc));
            if (!line.empty()) cfg.mods.push_back(line);
        }
    }
    return true;
}

// ---- real DLL forwarding ------------------------------------------------

#define BIND(n) fp_##n = GetProcAddress(real, #n);
void BindVersion(HMODULE real) { LIST_VERSION(BIND) }
void BindDInput8(HMODULE real) { LIST_DINPUT8(BIND) }
void BindDSound(HMODULE real) { LIST_DSOUND(BIND) }

struct ProxyTarget {
    const wchar_t* dll;
    void (*bind)(HMODULE);
};
const ProxyTarget kTargets[] = {
    {L"version.dll", BindVersion},
    {L"dinput8.dll", BindDInput8},
    {L"dsound.dll", BindDSound},
};

void BindRealDll() {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { Log(L"GetSystemDirectory failed"); return; }
    for (const auto& t : kTargets) {
        if (g_selfName != t.dll) continue;
        std::wstring p = std::wstring(sys) + L"\\" + t.dll;
        HMODULE real = LoadLibraryW(p.c_str());
        if (!real) { Log(L"Could not load real %ls (error %lu)", p.c_str(), GetLastError()); return; }
        t.bind(real);
        Log(L"Forwarding exports to %ls", p.c_str());
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

void LoadMods(const Config& cfg) {
    for (const auto& entry : cfg.mods) {
        std::wstring path = Resolve(entry);
        size_t slash = path.find_last_of(L'\\');
        if (Lower(path.substr(slash + 1)) == g_selfName) continue;  // never load ourselves
        HMODULE m = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m) Log(L"Loaded %ls", path.c_str());
        else Log(L"FAILED to load %ls (error %lu)", path.c_str(), GetLastError());
    }
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

    BindRealDll();  // first: the game may call the real exports immediately

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
