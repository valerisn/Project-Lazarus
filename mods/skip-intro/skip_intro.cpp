// skips the ~20s of logo videos.
//
// boot goes Startup -> Splashscreen (logos) -> Initialize -> Main Menu.
// nothing on the splash is worth calling, so we just do what it does when
// it's done: Load() the Initialize collection. if that's not in memory, crank
// timeScale until the next scene. that won't speed up the videos (they are
// videos, the census confirmed it) but it's a fallback

#include <windows.h>

#include <cstring>
#include <string>

#include "../dev-console/il2cpp.h"
#include "lazarus/lazarus_api.h"

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;

struct Cfg {
    std::string splash_scene = "Splashscreen";
    std::string target = "Initialize Scene Collection";
    float fallback_timescale = 20.0f;
    bool census = true;
} cfg;

int g_countdown = -1;          // -1 = not armed
bool g_fast_forwarding = false;
bool g_done = false;
bool g_timing = false;
ULONGLONG g_splash_start = 0;

#define LOG(lvl, ...) api->log(self, lvl, __VA_ARGS__)

void set_timescale(float v) {
    auto set = reinterpret_cast<void (*)(float)>(api->resolve_icall("UnityEngine.Time::set_timeScale"));
    if (set) set(v);
}

bool load_target() {
    il2::Klass* k = il2::find_class("Flux.LoadSceneManager.SceneCollection", "FluxSceneManager.dll");
    if (!k) {
        LOG(LAZARUS_LOG_WARN, "SceneCollection class not found");
        return false;
    }
    std::string err;
    for (il2::Obj* o : il2::find_all(k, err)) {
        if (_stricmp(il2::object_name(o).c_str(), cfg.target.c_str()) != 0) continue;
        il2::invoke(il2::find_method(il2::class_of(o), "Load", 0), o, nullptr, err);
        if (!err.empty()) {
            LOG(LAZARUS_LOG_WARN, "%s.Load() threw: %s", cfg.target.c_str(), err.c_str());
            return false;
        }
        return true;
    }
    LOG(LAZARUS_LOG_INFO, "'%s' isn't in memory%s", cfg.target.c_str(), err.empty() ? "" : (" (" + err + ")").c_str());
    return false;
}

void on_update(void*) {
    if (g_countdown < 0 || --g_countdown > 0) return;
    g_countdown = -1;
    if (load_target()) {
        LOG(LAZARUS_LOG_INFO, "skipped the intro logos (loaded %s)", cfg.target.c_str());
        g_done = true;
    } else if (cfg.fallback_timescale > 1.0f) {
        set_timescale(cfg.fallback_timescale);
        g_fast_forwarding = true;
        LOG(LAZARUS_LOG_INFO, "fast-forwarding the intro at %gx instead", cfg.fallback_timescale);
    }
}

void on_scene(void*, const char* name, int, int) {
    if (g_timing && _stricmp(name, cfg.splash_scene.c_str()) != 0) {
        // vanilla was 21.3s
        g_timing = false;
        LOG(LAZARUS_LOG_INFO, "intro took %.1fs this time (vanilla was ~21.3s on the dev machine)",
            (double)(GetTickCount64() - g_splash_start) / 1000.0);
    }
    if (g_fast_forwarding && _stricmp(name, cfg.splash_scene.c_str()) != 0) {
        // off the splash, put time back before anyone notices
        set_timescale(1.0f);
        g_fast_forwarding = false;
        g_done = true;
        LOG(LAZARUS_LOG_INFO, "intro over ('%s' loaded), timescale back to 1", name);
        return;
    }
    if (g_done || _stricmp(name, cfg.splash_scene.c_str()) != 0) return;
    g_splash_start = GetTickCount64();
    g_timing = true;

    // first try "didn't work": Load() fired fine, Initialize just took 9s cold
    // with the logos still up. so log what's actually in this scene
    if (cfg.census) {
        std::string err;
        auto lines = il2::census(err);
        if (!err.empty()) LOG(LAZARUS_LOG_WARN, "census failed: %s", err.c_str());
        LOG(LAZARUS_LOG_INFO, "census of '%s': %zu distinct component(s)", name, lines.size());
        for (auto& l : lines) LOG(LAZARUS_LOG_INFO, "  %s", l.c_str());
    }
    // next frame. calling Load() from inside the loaded-callback of the scene
    // we're about to replace felt like asking for trouble
    g_countdown = 1;
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    if (a->api_version < 2 || a->struct_size < sizeof(LazarusApi)) {
        a->log(m, LAZARUS_LOG_ERROR, "needs loader API v2");
        return 1;
    }
    if (!il2::init(a)) {
        a->log(m, LAZARUS_LOG_ERROR, "GameAssembly.dll is missing %s", il2::missing());
        return 2;
    }
    char buf[256];
    if (a->config_get_string(m, "skip", "splash_scene", buf, sizeof buf) && buf[0]) cfg.splash_scene = buf;
    if (a->config_get_string(m, "skip", "load_collection", buf, sizeof buf) && buf[0]) cfg.target = buf;
    cfg.fallback_timescale = (float)a->config_get_float(m, "skip", "fallback_timescale", 20.0);
    if (cfg.fallback_timescale < 1.0f || cfg.fallback_timescale > 100.0f) cfg.fallback_timescale = 20.0f;
    cfg.census = a->config_get_bool(m, "skip", "log_census", 1) != 0;

    if (a->on_update(m, &on_update, nullptr) != LAZARUS_OK) {
        a->log(m, LAZARUS_LOG_ERROR, "no frame tick available");
        return 3;
    }
    return a->on_scene_loaded(m, &on_scene, nullptr);
}

extern "C" __declspec(dllexport) void lazarus_mod_shutdown(LazarusMod*) {
    if (g_fast_forwarding) set_timescale(1.0f);
}
