// everything after il2cpp_init.
//
// rule for this file: never take the game down. mod code runs under SEH,
// failures turn into "that mod is off" plus a log line, and if the loader
// itself explodes the game just runs vanilla

#include "proxy/runtime.h"

#include <MinHook.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/command.h"
#include "core/config.h"
#include "core/ini.h"
#include "core/log.h"
#include "core/plan.h"
#include "core/state.h"
#include "lazarus/lazarus_api.h"

#ifndef LAZARUS_VERSION
#define LAZARUS_VERSION "dev"
#endif

namespace fs = std::filesystem;

extern "C" void* lz_real_version[17];  // filled in dllmain.cpp

// global namespace because that's where the header declared it
struct LazarusMod {
    lz::ModManifest manifest;
    std::string tag;  // "mod:<id>"
    std::string dir_utf8;
    lz::Ini config;
    HMODULE dll = nullptr;
    void (*shutdown)(LazarusMod*) = nullptr;
    std::vector<void*> hooks;
    bool active = false;
};

namespace lzrt {
namespace {

// ---------------------------------------------------------------- bootstrap

HMODULE g_proxy = nullptr;
wchar_t g_game_dir[MAX_PATH] = {};
std::mutex g_early_mu;
std::vector<std::string>* g_early = nullptr;  // never freed, it's 3 strings

fs::path lazarus_dir() { return fs::path(g_game_dir) / L"Lazarus"; }

// --------------------------------------------------------------- il2cpp api

struct Il2Cpp {
    void* (*domain_get)() = nullptr;
    void** (*domain_get_assemblies)(void* domain, size_t* size) = nullptr;
    void* (*assembly_get_image)(void* assembly) = nullptr;
    const char* (*image_get_name)(void* image) = nullptr;
    void* (*class_from_name)(void* image, const char* ns, const char* name) = nullptr;
    void* (*class_get_method_from_name)(void* klass, const char* name, int argc) = nullptr;
    void* (*resolve_icall)(const char* name) = nullptr;
    const wchar_t* (*string_chars)(void* str) = nullptr;
    int32_t (*string_length)(void* str) = nullptr;
};

HMODULE g_ga = nullptr;
Il2Cpp g_il2cpp;
bool g_il2cpp_ok = false;

template <typename T>
bool resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(g_ga, name));
    if (!fn) LZ_ERROR("loader", "GameAssembly.dll is missing export %s", name);
    return fn != nullptr;
}

bool resolve_il2cpp() {
    bool ok = true;
    ok &= resolve(g_il2cpp.domain_get, "il2cpp_domain_get");
    ok &= resolve(g_il2cpp.domain_get_assemblies, "il2cpp_domain_get_assemblies");
    ok &= resolve(g_il2cpp.assembly_get_image, "il2cpp_assembly_get_image");
    ok &= resolve(g_il2cpp.image_get_name, "il2cpp_image_get_name");
    ok &= resolve(g_il2cpp.class_from_name, "il2cpp_class_from_name");
    ok &= resolve(g_il2cpp.class_get_method_from_name, "il2cpp_class_get_method_from_name");
    ok &= resolve(g_il2cpp.resolve_icall, "il2cpp_resolve_icall");
    ok &= resolve(g_il2cpp.string_chars, "il2cpp_string_chars");
    ok &= resolve(g_il2cpp.string_length, "il2cpp_string_length");
    return ok;
}

void* find_method_impl(const char* assembly, const char* ns, const char* klass, const char* method, int argc) {
    if (!g_il2cpp_ok || !klass || !method) return nullptr;
    size_t n = 0;
    void** asms = g_il2cpp.domain_get_assemblies(g_il2cpp.domain_get(), &n);
    for (size_t i = 0; i < n; ++i) {
        void* img = g_il2cpp.assembly_get_image(asms[i]);
        if (!img) continue;
        if (assembly && *assembly) {
            const char* iname = g_il2cpp.image_get_name(img);
            if (!iname || _stricmp(iname, assembly) != 0) continue;
        }
        void* k = g_il2cpp.class_from_name(img, ns ? ns : "", klass);
        if (!k) continue;
        if (void* m = g_il2cpp.class_get_method_from_name(k, method, argc)) return m;
    }
    return nullptr;
}

// methodPointer is the first field of MethodInfo in 2021.x. if some other
// unity version moves it, this is what breaks
void* method_pointer_impl(void* mi) { return mi ? *reinterpret_cast<void**>(mi) : nullptr; }

std::string il2cpp_to_utf8(void* str) {
    if (!str) return {};
    const wchar_t* w = g_il2cpp.string_chars(str);
    int len = g_il2cpp.string_length(str);
    if (!w || len <= 0) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), need, nullptr, nullptr);
    return out;
}

// ------------------------------------------------------------------- state

std::vector<std::unique_ptr<LazarusMod>> g_mods;
LazarusMod g_loader_owner;  // fake mod so our own hooks go through the same bookkeeping

struct SceneCb {
    LazarusMod* mod;
    LazarusSceneLoadedFn fn;
    void* user;
    bool dead;
};
std::vector<SceneCb> g_scene_cbs;

std::string g_fingerprint;
bool g_build_known = false;
LazarusApi g_api{};

// ------------------------------------------------------------- SEH guards
// __try can't live in a function with destructors (C2712), so these stay tiny

using ModInitFn = int (*)(LazarusMod*, const LazarusApi*);

int guarded_init(ModInitFn fn, LazarusMod* m, const LazarusApi* api, DWORD* code) {
    __try {
        return fn(m, api);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return -1;
    }
}

bool guarded_shutdown(void (*fn)(LazarusMod*), LazarusMod* m, DWORD* code) {
    __try {
        fn(m);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

bool guarded_scene_cb(LazarusSceneLoadedFn fn, void* user, const char* name, int idx, int mode, DWORD* code) {
    __try {
        fn(user, name, idx, mode);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

// --------------------------------------------------------------- C API impl

void api_log(LazarusMod* self, LazarusLogLevel level, const char* fmt, ...) {
    if (!fmt) return;
    auto lvl = static_cast<lz::LogLevel>(level < 0 ? 0 : level > 4 ? 4 : level);
    va_list ap;
    va_start(ap, fmt);
    lz::Logger::global().vwritef(lvl, self ? self->tag : std::string("mod:?"), fmt, ap);
    va_end(ap);
}

int api_config_get_string(LazarusMod* self, const char* section, const char* key, char* buf, size_t len) {
    if (!self || !section || !key) return 0;
    auto v = self->config.get(section, key);
    if (buf && len) {
        const std::string& s = v ? *v : std::string();
        size_t n = s.size() < len - 1 ? s.size() : len - 1;
        std::memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    return v ? 1 : 0;
}

int64_t api_config_get_int(LazarusMod* self, const char* section, const char* key, int64_t def) {
    if (!self || !section || !key) return def;
    auto v = self->config.get(section, key);
    long long r = 0;
    if (!v) return def;
    if (!lz::parse_int(*v, r)) {
        LZ_WARN(self->tag, "config.ini [%s] %s = '%s' is not a whole number, using %lld", section, key, v->c_str(),
                (long long)def);
        return def;
    }
    return r;
}

double api_config_get_float(LazarusMod* self, const char* section, const char* key, double def) {
    if (!self || !section || !key) return def;
    auto v = self->config.get(section, key);
    double r = 0;
    if (!v) return def;
    if (!lz::parse_float(*v, r)) {
        LZ_WARN(self->tag, "config.ini [%s] %s = '%s' is not a number, using %g", section, key, v->c_str(), def);
        return def;
    }
    return r;
}

int api_config_get_bool(LazarusMod* self, const char* section, const char* key, int def) {
    if (!self || !section || !key) return def;
    auto v = self->config.get(section, key);
    bool r = false;
    if (!v) return def;
    if (!lz::parse_bool(*v, r)) {
        LZ_WARN(self->tag, "config.ini [%s] %s = '%s' is not true/false, using %s", section, key, v->c_str(),
                def ? "true" : "false");
        return def;
    }
    return r ? 1 : 0;
}

const char* api_mod_dir(LazarusMod* self) { return self ? self->dir_utf8.c_str() : ""; }

void* api_il2cpp_export(const char* name) {
    return (g_ga && name) ? reinterpret_cast<void*>(GetProcAddress(g_ga, name)) : nullptr;
}

void* api_resolve_icall(const char* name) {
    if (!g_il2cpp_ok || !name) return nullptr;
    return g_il2cpp.resolve_icall(name);
}

int api_hook_create(LazarusMod* self, void* target, void* detour, void** original) {
    if (!self || !target || !detour) return LAZARUS_ERR_INVALID_ARG;
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) {
        LZ_ERROR(self->tag, "hook at %p failed: %s", target, MH_StatusToString(s));
        MH_RemoveHook(target);
        return LAZARUS_ERR_HOOK_FAILED;
    }
    self->hooks.push_back(target);
    LZ_DEBUG(self->tag, "hooked %p", target);
    return LAZARUS_OK;
}

int api_hook_remove(LazarusMod* self, void* target) {
    if (!self || !target) return LAZARUS_ERR_INVALID_ARG;
    auto it = std::find(self->hooks.begin(), self->hooks.end(), target);
    if (it == self->hooks.end()) return LAZARUS_ERR_NOT_FOUND;
    MH_DisableHook(target);
    MH_RemoveHook(target);
    self->hooks.erase(it);
    return LAZARUS_OK;
}

int api_on_scene_loaded(LazarusMod* self, LazarusSceneLoadedFn fn, void* user) {
    if (!self || !fn) return LAZARUS_ERR_INVALID_ARG;
    g_scene_cbs.push_back({self, fn, user, false});
    return LAZARUS_OK;
}

// ------------------------------------------------- v2: update / tasks / cmds

struct UpdateCb {
    LazarusMod* mod;
    LazarusTaskFn fn;
    void* user;
    bool dead;
};
std::vector<UpdateCb> g_update_cbs;
bool g_update_ready = false;

struct Task {
    LazarusMod* mod;
    LazarusTaskFn fn;
    void* user;
};
std::mutex g_task_mu;
std::vector<Task> g_tasks;  // any thread pushes, main drains

lz::CommandRegistry g_cmds;

bool guarded_task(LazarusTaskFn fn, void* user, DWORD* code) {
    __try {
        fn(user);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

bool guarded_command(LazarusCommandFn fn, void* user, int argc, const char** argv, LazarusPrintFn print, void* ctx,
                     DWORD* code) {
    __try {
        fn(user, argc, argv, print, ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return false;
    }
}

int api_on_update(LazarusMod* self, LazarusTaskFn fn, void* user) {
    if (!self || !fn) return LAZARUS_ERR_INVALID_ARG;
    if (!g_update_ready) return LAZARUS_ERR_NOT_READY;
    g_update_cbs.push_back({self, fn, user, false});
    return LAZARUS_OK;
}

int api_run_on_main(LazarusMod* self, LazarusTaskFn fn, void* user) {
    if (!self || !fn) return LAZARUS_ERR_INVALID_ARG;
    if (!g_update_ready) return LAZARUS_ERR_NOT_READY;
    std::lock_guard<std::mutex> g(g_task_mu);
    g_tasks.push_back({self, fn, user});
    return LAZARUS_OK;
}

void print_trampoline(void* ctx, const char* text) {
    (*static_cast<const lz::CommandRegistry::Print*>(ctx))(text ? text : "");
}

int api_register_command(LazarusMod* self, const char* name, const char* help, LazarusCommandFn fn, void* user) {
    if (!self || !name || !fn) return LAZARUS_ERR_INVALID_ARG;
    std::string tag = self->tag;
    bool ok = g_cmds.add(name, help ? help : "", tag,
                         [fn, user, tag](const std::vector<std::string>& args, const lz::CommandRegistry::Print& print) {
                             std::vector<const char*> argv;
                             for (auto& a : args) argv.push_back(a.c_str());
                             DWORD code = 0;
                             if (!guarded_command(fn, user, (int)argv.size(), argv.data(), &print_trampoline,
                                                  (void*)&print, &code)) {
                                 LZ_ERROR(tag, "command '%s' crashed (exception 0x%08lX)", args[0].c_str(), code);
                                 print("command crashed, see lazarus.log");
                             }
                         });
    if (!ok) LZ_WARN(tag, "could not register command '%s' (invalid name or taken by another mod)", name);
    return ok ? LAZARUS_OK : LAZARUS_ERR_INVALID_ARG;
}

int api_execute_command(const char* line, LazarusPrintFn print, void* ctx) {
    if (!line) return LAZARUS_ERR_INVALID_ARG;
    lz::CommandRegistry::Print p = [print, ctx](const std::string& s) {
        if (print) print(ctx, s.c_str());
    };
    LZ_DEBUG("console", "> %s", line);
    auto r = g_cmds.execute(line, p);
    return r == lz::CommandRegistry::Result::NotFound ? LAZARUS_ERR_NOT_FOUND : LAZARUS_OK;
}

void drop_mod_hooks(LazarusMod* m) {
    for (void* t : m->hooks) {
        MH_DisableHook(t);
        MH_RemoveHook(t);
    }
    m->hooks.clear();
    for (auto& cb : g_scene_cbs)
        if (cb.mod == m) cb.dead = true;
    for (auto& cb : g_update_cbs)
        if (cb.mod == m) cb.dead = true;
    g_cmds.remove_owner(m->tag);
    std::lock_guard<std::mutex> g(g_task_mu);
    g_tasks.erase(std::remove_if(g_tasks.begin(), g_tasks.end(), [m](const Task& t) { return t.mod == m; }),
                  g_tasks.end());
}

// ------------------------------------------------------------ frame tick
//
// there's no nice per-frame hook in a stripped il2cpp build, so we piggyback
// on two things the engine calls every frame anyway: the input system's
// NotifyBeforeUpdate and BeforeRenderHelper.Invoke. both can fire more than
// once a frame, hence the frameCount check

using NotifyBeforeUpdate_t = void (*)(int32_t update_type, const void* method);
using BeforeRender_t = void (*)(const void* method);
NotifyBeforeUpdate_t g_orig_notify = nullptr;
BeforeRender_t g_orig_before_render = nullptr;
int32_t (*g_frame_count)() = nullptr;
int32_t g_last_frame = INT32_MIN;
bool g_in_tick = false;

void tick() {
    if (g_in_tick) return;  // a task that pumps the engine would re-enter us
    g_in_tick = true;

    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> g(g_task_mu);
        tasks.swap(g_tasks);
    }
    for (auto& t : tasks) {
        DWORD code = 0;
        if (!guarded_task(t.fn, t.user, &code))
            LZ_ERROR(t.mod->tag, "main-thread task crashed (exception 0x%08lX)", code);
    }

    if (!g_frame_count && g_il2cpp_ok)
        g_frame_count = reinterpret_cast<int32_t (*)()>(g_il2cpp.resolve_icall("UnityEngine.Time::get_frameCount"));
    int32_t frame = g_frame_count ? g_frame_count() : g_last_frame + 1;
    if (frame != g_last_frame) {
        g_last_frame = frame;
        for (size_t i = 0; i < g_update_cbs.size(); ++i) {
            auto cb = g_update_cbs[i];
            if (cb.dead) continue;
            DWORD code = 0;
            if (!guarded_task(cb.fn, cb.user, &code)) {
                g_update_cbs[i].dead = true;
                LZ_ERROR(cb.mod->tag, "update callback crashed (exception 0x%08lX), callback disabled", code);
            }
        }
    }
    g_in_tick = false;
}

void hk_notify_before_update(int32_t update_type, const void* method) {
    g_orig_notify(update_type, method);
    tick();
}

void hk_before_render(const void* method) {
    g_orig_before_render(method);
    tick();
}

void install_update_hook() {
    void* t1 = method_pointer_impl(find_method_impl("UnityEngine.InputModule.dll", "UnityEngineInternal.Input",
                                                    "NativeInputSystem", "NotifyBeforeUpdate", 1));
    void* t2 = method_pointer_impl(
        find_method_impl("UnityEngine.CoreModule.dll", "UnityEngine", "BeforeRenderHelper", "Invoke", 0));
    if (t1 && api_hook_create(&g_loader_owner, t1, reinterpret_cast<void*>(&hk_notify_before_update),
                              reinterpret_cast<void**>(&g_orig_notify)) == LAZARUS_OK)
        g_update_ready = true;
    if (t2 && api_hook_create(&g_loader_owner, t2, reinterpret_cast<void*>(&hk_before_render),
                              reinterpret_cast<void**>(&g_orig_before_render)) == LAZARUS_OK)
        g_update_ready = true;
    if (g_update_ready)
        LZ_INFO("loader", "frame tick hooked (input: %s, before-render: %s)", g_orig_notify ? "yes" : "no",
                g_orig_before_render ? "yes" : "no");
    else
        LZ_WARN("loader", "no per-frame engine callback found - on_update/run_on_main/console unavailable");
}

void register_builtin_commands() {
    g_cmds.add("help", "list commands", "loader", [](const std::vector<std::string>& args, const lz::CommandRegistry::Print& p) {
        std::string filter = args.size() > 1 ? lz::to_lower(args[1]) : "";
        for (auto& kv : g_cmds.entries())
            if (filter.empty() || kv.first.find(filter) != std::string::npos)
                p(kv.first + (kv.second.help.empty() ? "" : "  -  " + kv.second.help));
    });
    g_cmds.add("mods", "list loaded mods", "loader", [](const std::vector<std::string>&, const lz::CommandRegistry::Print& p) {
        for (auto& m : g_mods)
            p(m->manifest.id + " v" + m->manifest.version + (m->active ? "  active" : "  FAILED"));
    });
}

// ------------------------------------------------------------ scene hook

using SceneLoaded_t = void (*)(int32_t scene_handle, int32_t mode, const void* method);
SceneLoaded_t g_orig_scene_loaded = nullptr;
void* (*g_scene_get_name)(int32_t) = nullptr;
int32_t (*g_scene_get_build_index)(int32_t) = nullptr;

// SceneManager.Internal_SceneLoaded(Scene, LoadSceneMode). Scene is just an
// int handle in a struct, so it comes through as an int32
void hk_scene_loaded(int32_t handle, int32_t mode, const void* method) {
    // run the game's listeners first. if it resets stuff on scene load we
    // want to go after it, so our settings win
    g_orig_scene_loaded(handle, mode, method);

    std::string name = g_scene_get_name ? il2cpp_to_utf8(g_scene_get_name(handle)) : std::string();
    int idx = g_scene_get_build_index ? g_scene_get_build_index(handle) : -1;
    LZ_INFO("loader", "scene loaded: '%s' (build index %d, mode %d)", name.c_str(), idx, mode);

    // index loop on purpose, callbacks can add callbacks
    for (size_t i = 0; i < g_scene_cbs.size(); ++i) {
        auto cb = g_scene_cbs[i];
        if (cb.dead) continue;
        DWORD code = 0;
        if (!guarded_scene_cb(cb.fn, cb.user, name.c_str(), idx, mode, &code)) {
            g_scene_cbs[i].dead = true;
            LZ_ERROR(cb.mod->tag, "scene callback crashed (exception 0x%08lX), callback disabled", code);
        }
    }
}

void install_scene_hook() {
    void* mi = find_method_impl("UnityEngine.CoreModule.dll", "UnityEngine.SceneManagement", "SceneManager",
                                "Internal_SceneLoaded", 2);
    void* target = method_pointer_impl(mi);
    if (!target) {
        LZ_WARN("loader", "SceneManager.Internal_SceneLoaded not found - on_scene_loaded events will not fire");
        return;
    }
    g_scene_get_name = reinterpret_cast<void* (*)(int32_t)>(
        g_il2cpp.resolve_icall("UnityEngine.SceneManagement.Scene::GetNameInternal"));
    g_scene_get_build_index = reinterpret_cast<int32_t (*)(int32_t)>(
        g_il2cpp.resolve_icall("UnityEngine.SceneManagement.Scene::GetBuildIndexInternal"));
    // if these icalls aren't registered yet, scenes just log without names
    if (!g_scene_get_name) LZ_DEBUG("loader", "Scene::GetNameInternal icall not available yet");

    if (api_hook_create(&g_loader_owner, target, reinterpret_cast<void*>(&hk_scene_loaded),
                        reinterpret_cast<void**>(&g_orig_scene_loaded)) == LAZARUS_OK)
        LZ_INFO("loader", "scene load hook installed at %p", target);
}

// ------------------------------------------------------------- mod loading

void load_one(const lz::ModManifest& man, lz::BootGuard& guard) {
    auto m = std::make_unique<LazarusMod>();
    m->manifest = man;
    m->tag = "mod:" + man.id;
    m->dir_utf8 = man.dir.u8string();

    auto cfg_path = man.dir / "config.ini";
    std::string err;
    if (fs::exists(cfg_path) && lz::Ini::load(cfg_path, m->config, err))
        for (auto& e : m->config.errors()) LZ_WARN(m->tag, "config.ini line %d: %s", e.line, e.message.c_str());

    auto dll_path = man.dir / fs::u8path(man.dll);
    guard.begin("mod:" + man.id);

    // so a mod can ship its own dependency dlls next to it
    m->dll = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m->dll) {
        LZ_ERROR(m->tag, "could not load %s (Win32 error %lu)", man.dll.c_str(), GetLastError());
        guard.end();
        return;
    }
    auto init = reinterpret_cast<ModInitFn>(GetProcAddress(m->dll, "lazarus_mod_init"));
    m->shutdown = reinterpret_cast<void (*)(LazarusMod*)>(GetProcAddress(m->dll, "lazarus_mod_shutdown"));
    if (!init) {
        LZ_ERROR(m->tag, "%s does not export lazarus_mod_init, not a Lazarus mod", man.dll.c_str());
        FreeLibrary(m->dll);
        guard.end();
        return;
    }

    DWORD code = 0;
    int rc = guarded_init(init, m.get(), &g_api, &code);
    guard.end();
    if (code) {
        // who knows what state it's in. pull the hooks but leave the dll
        // mapped, unloading code that might still be referenced is how a
        // logged error becomes a crash
        LZ_ERROR(m->tag, "crashed during init (exception 0x%08lX) - disabled for this session", code);
        drop_mod_hooks(m.get());
    } else if (rc != 0) {
        LZ_ERROR(m->tag, "init returned %d - disabled for this session", rc);
        drop_mod_hooks(m.get());
        FreeLibrary(m->dll);
        m->dll = nullptr;
    } else {
        m->active = true;
        LZ_INFO(m->tag, "loaded %s v%s", man.name.c_str(), man.version.c_str());
    }
    g_mods.push_back(std::move(m));
}

std::string unity_version() {
    // we ARE version.dll, can't link version.lib. use the forward table
    // (indices from kVersionExports in dllmain.cpp)
    using SizeW_t = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoW_t = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryW_t = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto size_w = reinterpret_cast<SizeW_t>(lz_real_version[7]);
    auto info_w = reinterpret_cast<InfoW_t>(lz_real_version[8]);
    auto query_w = reinterpret_cast<QueryW_t>(lz_real_version[16]);

    wchar_t path[MAX_PATH];
    HMODULE up = GetModuleHandleW(L"UnityPlayer.dll");
    if (!up || !GetModuleFileNameW(up, path, MAX_PATH)) return "unknown";
    DWORD dummy = 0, size = size_w(path, &dummy);
    if (!size) return "unknown";
    std::vector<BYTE> buf(size);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!info_w(path, 0, size, buf.data()) || !query_w(buf.data(), L"\\", (void**)&fi, &len) || !fi)
        return "unknown";
    char out[64];
    std::snprintf(out, sizeof out, "%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
                  HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
    return out;
}

bool safe_mode_requested(std::string& why) {
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        why = "SHIFT held during startup";
        return true;
    }
    wchar_t buf[8];
    if (GetEnvironmentVariableW(L"LAZARUS_SAFE_MODE", buf, 8) && buf[0] == L'1') {
        why = "LAZARUS_SAFE_MODE=1";
        return true;
    }
    return false;
}

void start() {
    auto root = lazarus_dir();
    auto& log = lz::Logger::global();
    log.open(root / "logs" / "lazarus.log");
    {
        // keep the windows username out of logs people paste on github
        wchar_t prof[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
        if (n && n < MAX_PATH) {
            std::string p = fs::path(prof).u8string();
            log.add_redaction(p, "%USERPROFILE%");
            std::string fwd = p;
            for (auto& c : fwd)
                if (c == '\\') c = '/';
            log.add_redaction(fwd, "%USERPROFILE%");
        }
    }

    auto cfg = lz::load_config(root / "lazarus.ini");
    log.set_level(cfg.log_level);
    LZ_INFO("loader", "Project Lazarus %s starting", LAZARUS_VERSION);
    {
        std::lock_guard<std::mutex> g(g_early_mu);
        if (g_early)
            for (auto& s : *g_early) LZ_DEBUG("boot", "%s", s.c_str());
    }
    LZ_INFO("loader", "game dir: %s", fs::path(g_game_dir).u8string().c_str());
    LZ_INFO("loader", "Unity: %s", unity_version().c_str());
    for (auto& w : cfg.warnings) LZ_WARN("config", "%s", w.c_str());

    if (!cfg.enabled) {
        LZ_INFO("loader", "disabled in lazarus.ini ([loader] enabled = false), doing nothing");
        return;
    }

    wchar_t ga_path[MAX_PATH] = {};
    GetModuleFileNameW(g_ga, ga_path, MAX_PATH);
    g_fingerprint = lz::build_fingerprint(ga_path, root / "state" / "fingerprint.cache");
    if (auto kb = lz::find_known_build(g_fingerprint)) {
        g_build_known = true;
        LZ_INFO("loader", "game build %s: %s", g_fingerprint.c_str(), kb->description);
    } else {
        LZ_WARN("loader", "game build %s is not one Lazarus has been tested on. Build-pinned mods will be skipped.",
                g_fingerprint.empty() ? "<unreadable>" : g_fingerprint.c_str());
    }

    g_il2cpp_ok = resolve_il2cpp();
    if (!g_il2cpp_ok) {
        LZ_ERROR("loader", "IL2CPP API incomplete, not loading any mods. Game continues unmodified.");
        return;
    }
    MH_STATUS mh = MH_Initialize();
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
        LZ_ERROR("loader", "MinHook init failed: %s. Not loading mods.", MH_StatusToString(mh));
        return;
    }

    g_api.struct_size = sizeof(LazarusApi);
    g_api.api_version = LAZARUS_API_VERSION;
    g_api.loader_version = LAZARUS_VERSION;
    g_api.game_build = g_fingerprint.c_str();
    g_api.game_build_known = g_build_known ? 1 : 0;
    g_api.log = &api_log;
    g_api.config_get_string = &api_config_get_string;
    g_api.config_get_int = &api_config_get_int;
    g_api.config_get_float = &api_config_get_float;
    g_api.config_get_bool = &api_config_get_bool;
    g_api.mod_dir = &api_mod_dir;
    g_api.il2cpp_export = &api_il2cpp_export;
    g_api.resolve_icall = &api_resolve_icall;
    g_api.find_method = &find_method_impl;
    g_api.method_pointer = &method_pointer_impl;
    g_api.hook_create = &api_hook_create;
    g_api.hook_remove = &api_hook_remove;
    g_api.on_scene_loaded = &api_on_scene_loaded;
    g_api.on_update = &api_on_update;
    g_api.run_on_main = &api_run_on_main;
    g_api.register_command = &api_register_command;
    g_api.execute_command = &api_execute_command;

    g_loader_owner.tag = "loader";
    install_scene_hook();
    install_update_hook();
    register_builtin_commands();

    lz::BootGuard guard(root / "state");
    auto stale = guard.take_stale_lock();
    if (!stale.empty()) {
        if (stale.rfind("mod:", 0) == 0) {
            auto id = stale.substr(4);
            guard.quarantine(id, "game crashed or was killed while this mod was starting");
            LZ_ERROR("loader", "last session died while '%s' was starting - it is now quarantined", id.c_str());
        } else {
            LZ_WARN("loader", "last session died during '%s'", stale.c_str());
        }
    }

    std::string why;
    if (!cfg.safe_mode && safe_mode_requested(why)) {
        cfg.safe_mode = true;
        LZ_WARN("loader", "safe mode: %s", why.c_str());
    } else if (cfg.safe_mode) {
        LZ_WARN("loader", "safe mode: enabled in lazarus.ini");
    }

    std::vector<std::string> warnings;
    auto mods = lz::discover_mods(root / "mods", warnings);
    for (auto& d : mods)
        for (auto& w : d.warnings) LZ_WARN("mods", "%s: %s", d.folder.c_str(), w.c_str());

    lz::PlanInput in;
    in.loader_api_version = LAZARUS_API_VERSION;
    in.build_fingerprint = g_fingerprint;
    in.quarantined = guard.load_quarantine();
    auto plan = lz::plan_load(mods, cfg, in);
    for (auto& w : warnings) LZ_WARN("mods", "%s", w.c_str());
    for (auto& w : plan.warnings) LZ_WARN("mods", "%s", w.c_str());
    for (auto& s : plan.skipped) LZ_INFO("mods", "skipping %s: %s", s.id.c_str(), s.reason.c_str());
    LZ_INFO("mods", "%zu mod(s) found, %zu to load", mods.size(), plan.load_order.size());

    for (auto& man : plan.load_order) {
        bool deps_ok = true;
        for (auto& dep : man.requires_) {
            bool up = false;
            for (auto& m : g_mods)
                if (m->manifest.id == dep && m->active) up = true;
            if (!up) {
                LZ_ERROR("mod:" + man.id, "dependency '%s' failed to load, skipping", dep.c_str());
                deps_ok = false;
                break;
            }
        }
        if (deps_ok) load_one(man, guard);
    }
    size_t active = 0;
    for (auto& m : g_mods) active += m->active ? 1 : 0;
    LZ_INFO("loader", "startup complete: %zu mod(s) active", active);
}

int guarded_start(DWORD* code) {
    __try {
        start();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *code = GetExceptionCode();
        return -1;
    }
}

}  // namespace

void set_proxy_module(HMODULE self) {
    g_proxy = self;
    // we sit next to the exe, so our folder is the game folder
    DWORD n = GetModuleFileNameW(self, g_game_dir, MAX_PATH);
    if (n && n < MAX_PATH) {
        wchar_t* slash = wcsrchr(g_game_dir, L'\\');
        if (slash) *slash = 0;
    }
}

void bootstrap_note(const char* msg) {
    // runs inside DllMain, raw win32 only
    wchar_t path[MAX_PATH + 64];
    if (g_game_dir[0]) {
        swprintf_s(path, L"%s\\Lazarus\\logs", g_game_dir);
        CreateDirectoryW(path, nullptr);
        wcscat_s(path, L"\\bootstrap.log");
        static bool truncated = false;
        HANDLE h = CreateFileW(path, truncated ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               truncated ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        truncated = true;
        if (h != INVALID_HANDLE_VALUE) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char line[512];
            int n = _snprintf_s(line, _TRUNCATE, "%02d:%02d:%02d.%03d %s\r\n", st.wHour, st.wMinute, st.wSecond,
                                st.wMilliseconds, msg);
            DWORD w = 0;
            if (n > 0) WriteFile(h, line, (DWORD)n, &w, nullptr);
            CloseHandle(h);
        }
    }
    std::lock_guard<std::mutex> g(g_early_mu);
    if (!g_early) g_early = new std::vector<std::string>();
    g_early->emplace_back(msg);
}

void on_il2cpp_ready(HMODULE game_assembly) {
    static bool started = false;
    if (started) return;  // unity only calls it once. probably
    started = true;
    g_ga = game_assembly;
    DWORD code = 0;
    if (guarded_start(&code) != 0)
        LZ_ERROR("loader", "loader crashed during startup (exception 0x%08lX). Game continues; mods may be partly loaded.",
                 code);
}

void on_il2cpp_shutdown() {
    LZ_INFO("loader", "game shutting down");
    for (auto it = g_mods.rbegin(); it != g_mods.rend(); ++it) {
        auto& m = *it;
        if (!m->active) continue;
        if (m->shutdown) {
            DWORD code = 0;
            if (!guarded_shutdown(m->shutdown, m.get(), &code))
                LZ_ERROR(m->tag, "crashed during shutdown (exception 0x%08lX)", code);
        }
        drop_mod_hooks(m.get());
        m->active = false;
    }
    g_update_ready = false;
    drop_mod_hooks(&g_loader_owner);
    MH_Uninitialize();
    LZ_INFO("loader", "shutdown complete");
    lz::Logger::global().close();
}

}  // namespace lzrt
