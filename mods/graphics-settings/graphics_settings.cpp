// Graphics Settings - first Project Lazarus fix.
//
// The game's own C# never calls targetFrameRate / vSyncCount / SetResolution /
// SetQualityLevel - IL2CPP stripped all of those managed wrappers because
// nothing referenced them. So there's nothing to hook; we call the engine's
// native internal calls (icalls) directly instead.
//
// Everything is applied on scene load, the first point where the engine is
// guaranteed to be up. After that we re-check on every scene load and
// re-apply if something (the game, a quality preset switch) changed it.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "lazarus/lazarus_api.h"
#include "settings.h"

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;
gfx::Settings cfg;
bool applied_once = false;
bool disabled = false;

#define LOG(lvl, ...) api->log(self, lvl, __VA_ARGS__)

// icall signatures are the managed signature minus the MethodInfo*, as plain C
struct Icalls {
    void (*set_fps)(int32_t) = nullptr;
    int32_t (*get_fps)() = nullptr;
    void (*set_vsync)(int32_t) = nullptr;
    int32_t (*get_vsync)() = nullptr;
    void (*set_quality)(int32_t, bool) = nullptr;
    int32_t (*get_quality)() = nullptr;
    void* (*get_quality_names)() = nullptr;
    void (*set_lod)(float) = nullptr;
    float (*get_lod)() = nullptr;
    void (*set_aniso)(int32_t) = nullptr;
    int32_t (*get_aniso)() = nullptr;
    void (*set_resolution)(int32_t, int32_t, int32_t, int32_t) = nullptr;
    int32_t (*screen_w)() = nullptr;
    int32_t (*screen_h)() = nullptr;
    int32_t (*get_mode)() = nullptr;
} ic;

uint32_t (*array_length)(void*) = nullptr;
const wchar_t* (*string_chars)(void*) = nullptr;
int32_t (*string_length)(void*) = nullptr;

template <typename T>
bool icall(T& fn, const char* name, bool needed) {
    fn = reinterpret_cast<T>(api->resolve_icall(name));
    if (!fn && needed) LOG(LAZARUS_LOG_ERROR, "engine call %s not available - that setting can't be applied", name);
    return fn != nullptr;
}

void resolve_all() {
    // only complain about the ones the user actually configured
    icall(ic.set_fps, "UnityEngine.Application::set_targetFrameRate", cfg.target_fps != 0);
    icall(ic.get_fps, "UnityEngine.Application::get_targetFrameRate", false);
    icall(ic.set_vsync, "UnityEngine.QualitySettings::set_vSyncCount", cfg.vsync >= 0);
    icall(ic.get_vsync, "UnityEngine.QualitySettings::get_vSyncCount", false);
    icall(ic.set_quality, "UnityEngine.QualitySettings::SetQualityLevel", cfg.quality_level >= 0);
    icall(ic.get_quality, "UnityEngine.QualitySettings::GetQualityLevel", false);
    icall(ic.get_quality_names, "UnityEngine.QualitySettings::get_names", false);
    icall(ic.set_lod, "UnityEngine.QualitySettings::set_lodBias", cfg.lod_bias > 0);
    icall(ic.get_lod, "UnityEngine.QualitySettings::get_lodBias", false);
    icall(ic.set_aniso, "UnityEngine.QualitySettings::set_anisotropicFiltering", cfg.anisotropic >= 0);
    icall(ic.get_aniso, "UnityEngine.QualitySettings::get_anisotropicFiltering", false);
    icall(ic.set_resolution, "UnityEngine.Screen::SetResolution", cfg.touches_display());
    icall(ic.screen_w, "UnityEngine.Screen::get_width", false);
    icall(ic.screen_h, "UnityEngine.Screen::get_height", false);
    icall(ic.get_mode, "UnityEngine.Screen::get_fullScreenMode", false);
    array_length = reinterpret_cast<uint32_t (*)(void*)>(api->il2cpp_export("il2cpp_array_length"));
    string_chars = reinterpret_cast<const wchar_t* (*)(void*)>(api->il2cpp_export("il2cpp_string_chars"));
    string_length = reinterpret_cast<int32_t (*)(void*)>(api->il2cpp_export("il2cpp_string_length"));
}

std::string to_utf8(void* str) {
    if (!str || !string_chars || !string_length) return "?";
    std::string out;
    const wchar_t* w = string_chars(str);
    for (int32_t i = 0, n = string_length(str); i < n; ++i)
        out += (w[i] < 128) ? (char)w[i] : '?';  // quality level names are ascii, good enough for a log line
    return out;
}

int log_quality_levels() {
    if (!ic.get_quality_names || !array_length) return -1;
    void* arr = ic.get_quality_names();
    if (!arr) return -1;
    uint32_t n = array_length(arr);
    // Il2CppArray: object header (16) + bounds ptr (8) + max_length (8), then elements
    auto items = reinterpret_cast<void**>(reinterpret_cast<char*>(arr) + 32);
    std::string list;
    for (uint32_t i = 0; i < n; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%s%u=", i ? ", " : "", i);
        list += buf;
        list += to_utf8(items[i]);
    }
    LOG(LAZARUS_LOG_INFO, "quality levels: %s (current %d)", list.c_str(), ic.get_quality ? ic.get_quality() : -1);
    return (int)n;
}

// set -> read back -> complain if it didn't stick. on later scenes, also
// notice if something changed it behind our back.
template <typename T>
void apply(const char* what, T want, T (*get)(), void (*set)(T), bool first) {
    if (!set) return;
    if (get) {
        T cur = get();
        if (cur == want) {
            if (first) LOG(LAZARUS_LOG_INFO, "%s already %g", what, (double)want);
            return;
        }
        if (!first) LOG(LAZARUS_LOG_WARN, "%s was changed to %g by the game, putting it back to %g", what, (double)cur, (double)want);
    }
    set(want);
    if (get) {
        T now = get();
        if (now != want)
            LOG(LAZARUS_LOG_WARN, "%s: asked for %g, engine reports %g", what, (double)want, (double)now);
        else if (first)
            LOG(LAZARUS_LOG_INFO, "%s set to %g", what, (double)want);
    } else if (first) {
        LOG(LAZARUS_LOG_INFO, "%s set to %g (engine has no getter to verify)", what, (double)want);
    }
}

void apply_display() {
    if (!ic.set_resolution || !cfg.touches_display()) return;
    int w = cfg.width ? cfg.width : (ic.screen_w ? ic.screen_w() : 0);
    int h = cfg.height ? cfg.height : (ic.screen_h ? ic.screen_h() : 0);
    int mode = cfg.window_mode != gfx::kLeave ? cfg.window_mode : (ic.get_mode ? ic.get_mode() : gfx::kBorderless);
    if (w <= 0 || h <= 0) {
        LOG(LAZARUS_LOG_WARN, "couldn't work out the current resolution, leaving display settings alone");
        return;
    }
    // Screen.SetResolution is applied by unity at the end of the frame, so a
    // read-back here would still show the old values. log intent only.
    ic.set_resolution(w, h, mode, cfg.refresh_rate);
    LOG(LAZARUS_LOG_INFO, "display: %dx%d %s%s", w, h, gfx::window_mode_name(mode),
        cfg.refresh_rate ? (" @" + std::to_string(cfg.refresh_rate) + "Hz").c_str() : "");
}

void apply_all(bool first) {
    if (first) {
        int levels = log_quality_levels();
        if (cfg.quality_level >= 0 && levels >= 0 && cfg.quality_level >= levels) {
            LOG(LAZARUS_LOG_WARN, "quality_level %d doesn't exist (game has %d levels), ignoring it", cfg.quality_level, levels);
            cfg.quality_level = -1;
        }
        apply_display();
    }
    // quality level first: switching presets resets vsync/lod/aniso to that
    // preset's values, so everything else has to go on top of it
    if (cfg.quality_level >= 0 && ic.set_quality) {
        int cur = ic.get_quality ? ic.get_quality() : -1;
        if (cur != cfg.quality_level) {
            if (!first) LOG(LAZARUS_LOG_WARN, "quality level was changed to %d by the game, putting it back to %d", cur, cfg.quality_level);
            ic.set_quality(cfg.quality_level, true);
            int now = ic.get_quality ? ic.get_quality() : cfg.quality_level;
            LOG(now == cfg.quality_level ? LAZARUS_LOG_INFO : LAZARUS_LOG_WARN, "quality level: asked for %d, now %d",
                cfg.quality_level, now);
        }
    }
    if (cfg.vsync >= 0) apply<int32_t>("vsync", cfg.vsync, ic.get_vsync, ic.set_vsync, first);
    if (cfg.target_fps != 0) apply<int32_t>("frame rate cap", cfg.target_fps, ic.get_fps, ic.set_fps, first);
    if (cfg.lod_bias > 0) apply<float>("LOD bias", (float)cfg.lod_bias, ic.get_lod, ic.set_lod, first);
    if (cfg.anisotropic >= 0) apply<int32_t>("anisotropic filtering", cfg.anisotropic, ic.get_aniso, ic.set_aniso, first);
}

// Watchdog (issue #8). First live run: cap applied at the first scene, yet
// the first 10s still averaged 368 fps with no drift seen at scene loads -
// something flips it between scene loads and flips it back before the next
// one. Prime suspect is the boot video sequence. Checking two getters every
// 30 frames is basically free, so just keep putting it back and say who/when.
int g_watch_frames = 0;
int g_watch_logs = 0;

void watch_one(const char* what, int32_t want, int32_t (*get)(), void (*set)(int32_t)) {
    if (!get || !set) return;
    int32_t cur = get();
    if (cur == want) return;
    set(want);
    // first few in full, then every 50th, so a game that fights us every
    // frame doesn't turn lazarus.log into a novel
    if (g_watch_logs < 5 || g_watch_logs % 50 == 0)
        LOG(LAZARUS_LOG_WARN, "watchdog: %s was %d, put back to %d (correction #%d)", what, cur, want, g_watch_logs + 1);
    ++g_watch_logs;
}

void on_update(void*) {
    if (disabled || !applied_once || ++g_watch_frames < 30) return;
    g_watch_frames = 0;
    if (cfg.vsync >= 0) watch_one("vsync", cfg.vsync, ic.get_vsync, ic.set_vsync);
    if (cfg.target_fps != 0) watch_one("frame rate cap", cfg.target_fps, ic.get_fps, ic.set_fps);
}

void on_scene(void*, const char* name, int, int mode) {
    if (disabled) return;
    if (!applied_once) {
        resolve_all();
        LOG(LAZARUS_LOG_INFO, "applying settings (first scene '%s')", name);
        apply_all(true);
        applied_once = true;
    } else if (cfg.reapply_each_scene && mode == 0) {
        // additive loads don't swap out the world, the game has no reason
        // to touch settings there. skipping them saves a few calls per load.
        apply_all(false);
    }
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    // built against the v2 header but only v1 fields are required
    if (a->struct_size < offsetof(LazarusApi, on_update)) return 1;

    std::vector<std::string> warnings;
    cfg = gfx::parse(
        [](const char* sec, const char* key) -> std::optional<std::string> {
            char buf[256];
            if (!api->config_get_string(self, sec, key, buf, sizeof buf)) return std::nullopt;
            return std::string(buf);
        },
        warnings);
    for (auto& w : warnings) LOG(LAZARUS_LOG_WARN, "config.ini: %s", w.c_str());

    bool anything = cfg.touches_display() || cfg.target_fps || cfg.vsync >= 0 || cfg.quality_level >= 0 ||
                    cfg.lod_bias > 0 || cfg.anisotropic >= 0;
    if (!anything) {
        LOG(LAZARUS_LOG_INFO, "config.ini has nothing set, the game's defaults are left alone. Edit %s\\config.ini.",
            api->mod_dir(self));
        return 0;  // loaded but idle - not an error
    }
    // v2 loaders give us a frame tick for the watchdog; on v1 we still work,
    // just with scene-load re-applies only
    if (api->api_version >= 2 && (cfg.target_fps != 0 || cfg.vsync >= 0) &&
        api->on_update(self, &on_update, nullptr) != LAZARUS_OK)
        LOG(LAZARUS_LOG_WARN, "no frame tick, frame cap watchdog off");
    return api->on_scene_loaded(self, &on_scene, nullptr);
}

extern "C" __declspec(dllexport) void lazarus_mod_shutdown(LazarusMod*) { disabled = true; }
