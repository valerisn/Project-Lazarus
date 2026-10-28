// Skip Intro: gets you past the ~20s of publisher/developer logos.
//
// Boot order on build 12839205 (from the first live log):
//   Startup -> Splashscreen (logos, ~21s) -> Initialize -> Main Menu
//
// LogoScreenUI has nothing to call ("Start/Update", no fields), so instead
// of poking the splash we do what the splash does at the end: Load() the
// game's own "Initialize" SceneCollection. If that asset isn't in memory
// for some reason, fall back to cranking Time.timeScale until the next scene
// shows up. The fallback won't speed up a video if the logos are one, but
// it's better than nothing.

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

int g_countdown = -1;          // frames until we act, -1 = idle
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
        // baseline from the first live run without this mod: 21.3s
        g_timing = false;
        LOG(LAZARUS_LOG_INFO, "intro took %.1fs this time (vanilla was ~21.3s on the dev machine)",
            (double)(GetTickCount64() - g_splash_start) / 1000.0);
    }
    if (g_fast_forwarding && _stricmp(name, cfg.splash_scene.c_str()) != 0) {
        // left the splash one way or another - put time back to normal before anyone notices
        set_timescale(1.0f);
        g_fast_forwarding = false;
        g_done = true;
        LOG(LAZARUS_LOG_INFO, "intro over ('%s' loaded), timescale back to 1", name);
        return;
    }
    if (g_done || _stricmp(name, cfg.splash_scene.c_str()) != 0) return;
    g_splash_start = GetTickCount64();
    g_timing = true;

    // first live attempt: Load() fired fine but Initialize then took 9s to
    // load with the logos still up, so "skipped" looked exactly like "didn't
    // work". before guessing again, write down what this scene actually is.
    if (cfg.census) {
        std::string err;
        auto lines = il2::census(err);
        if (!err.empty()) LOG(LAZARUS_LOG_WARN, "census failed: %s", err.c_str());
        LOG(LAZARUS_LOG_INFO, "census of '%s': %zu distinct component(s)", name, lines.size());
        for (auto& l : lines) LOG(LAZARUS_LOG_INFO, "  %s", l.c_str());
    }
    // next frame, not this one: calling Load() from inside the scene-loaded
    // callback of the very scene we're replacing felt like asking for it
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
