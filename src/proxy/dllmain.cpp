// the version.dll proxy. UnityPlayer imports VERSION.dll and since it's not a
// KnownDLL, ours in the game folder wins over System32.
//
// DllMain does as little as possible (loader lock): load the real version.dll
// and patch UnityPlayer's GetProcAddress import. later, when Unity looks up
// il2cpp_init/il2cpp_shutdown in GameAssembly, it gets our wrappers instead,
// and the real loader work starts in runtime.cpp once il2cpp is up.

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "proxy/iat.h"
#include "proxy/runtime.h"

extern "C" void* lz_real_version[17] = {};

namespace {

const char* const kVersionExports[17] = {
    "GetFileVersionInfoA",     "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",   "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA",  "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",     "VerFindFileA",               "VerFindFileW",            "VerInstallFileA",
    "VerInstallFileW",         "VerLanguageNameA",           "VerLanguageNameW",        "VerQueryValueA",
    "VerQueryValueW",
};

// everything in version.dll returns BOOL/DWORD, so 0 is a fine failure.
// better than jumping to null anyway
extern "C" ULONG_PTR lz_missing_export() {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

bool forward_init() {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 16) return false;
    wcscat_s(path, L"\\version.dll");
    // yes, LoadLibrary in DllMain. the real version.dll only needs kernel32
    // and ntdll which are already in, so there's no deadlock risk
    HMODULE real = LoadLibraryW(path);
    for (int i = 0; i < 17; ++i) {
        void* p = real ? reinterpret_cast<void*>(GetProcAddress(real, kVersionExports[i])) : nullptr;
        lz_real_version[i] = p ? p : reinterpret_cast<void*>(&lz_missing_export);
    }
    return real != nullptr;
}

using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using il2cpp_init_t = int (*)(const char*);
using il2cpp_shutdown_t = void (*)();

il2cpp_init_t g_real_init = nullptr;
il2cpp_shutdown_t g_real_shutdown = nullptr;
HMODULE g_game_assembly = nullptr;

int lz_il2cpp_init(const char* domain_name) {
    int r = g_real_init(domain_name);
    lzrt::on_il2cpp_ready(g_game_assembly);
    return r;
}

void lz_il2cpp_shutdown() {
    lzrt::on_il2cpp_shutdown();
    g_real_shutdown();
}

FARPROC WINAPI hooked_GetProcAddress(HMODULE mod, LPCSTR name) {
    FARPROC real = GetProcAddress(mod, name);  // ours isn't patched, only UnityPlayer's
    // ordinal lookup, not a string
    if (!real || (reinterpret_cast<ULONG_PTR>(name) >> 16) == 0) return real;
    if (name[0] != 'i' || std::strncmp(name, "il2cpp_", 7) != 0) return real;

    if (std::strcmp(name, "il2cpp_init") == 0) {
        g_real_init = reinterpret_cast<il2cpp_init_t>(real);
        g_game_assembly = mod;
        lzrt::bootstrap_note("intercepted il2cpp_init");
        return reinterpret_cast<FARPROC>(&lz_il2cpp_init);
    }
    if (std::strcmp(name, "il2cpp_shutdown") == 0) {
        g_real_shutdown = reinterpret_cast<il2cpp_shutdown_t>(real);
        return reinterpret_cast<FARPROC>(&lz_il2cpp_shutdown);
    }
    return real;
}

void attach() {
    bool fwd = forward_init();

    HMODULE unity = GetModuleHandleW(L"UnityPlayer.dll");
    if (!unity) {
        // not the game (the crash handler loads us too, found that out on the
        // first real run). don't write anything, it used to wipe bootstrap.log
        return;
    }
    lzrt::bootstrap_note(fwd ? "forwarding to system version.dll" : "FAILED to load system version.dll");
    void* orig = nullptr;
    if (lzrt::patch_iat(unity, "KERNEL32.dll", "GetProcAddress", reinterpret_cast<void*>(&hooked_GetProcAddress), &orig))
        lzrt::bootstrap_note("patched UnityPlayer.dll GetProcAddress import");
    else
        lzrt::bootstrap_note("FAILED to patch UnityPlayer.dll GetProcAddress import - loader will not start");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        lzrt::set_proxy_module(self);
        attach();
    }
    return TRUE;
}
