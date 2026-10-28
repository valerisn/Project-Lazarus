// Mouse Sensitivity (issue #2): "impossible to aim unless your DPI is at 10".
//
// All camera look input in this game goes through Cinemachine's
// CinemachineInputProvider.GetAxisValue(axis) (axis 0 = X, 1 = Y, 2 = Z).
// Scaling X/Y there is upstream of the game's own sensitivity slider, so the
// slider still works, just on top of our scale.
//
// Mouse vs gamepad: the game itself calls PlayerInputsReader.OnKeyboardActive
// (bool) when you switch devices, so we hook that to know which scale to use.
// Gamepad players complained less, so the two scales are separate.

#include <windows.h>

#include <cmath>
#include <cstdint>

#include "../dev-console/il2cpp.h"
#include "lazarus/lazarus_api.h"

namespace {

const LazarusApi* api = nullptr;
LazarusMod* self = nullptr;

float g_mouse_scale = 1.0f, g_pad_scale = 1.0f;
float g_y_ratio = 1.0f;          // extra multiplier on vertical only
volatile bool g_kbm = true;      // until the game tells us otherwise
bool g_seen_device_event = false;

// calibration stats, logged every 10s at debug when there was look input
float g_peak_raw = 0, g_peak_scaled = 0;
ULONGLONG g_last_stats = 0;

using GetAxis_t = float (*)(void* self, int32_t axis, const void* method);
using OnKbm_t = void (*)(void* self, bool use, const void* method);
GetAxis_t g_orig_get_axis = nullptr;
OnKbm_t g_orig_on_kbm = nullptr;

float hk_get_axis(void* obj, int32_t axis, const void* method) {
    float v = g_orig_get_axis(obj, axis, method);
    if (axis > 1) return v;
    float s = g_kbm ? g_mouse_scale : g_pad_scale;
    if (axis == 1) s *= g_y_ratio;
    float out = v * s;
    float a = std::fabs(v);
    if (a > g_peak_raw) {
        g_peak_raw = a;
        g_peak_scaled = std::fabs(out);
    }
    return out;
}

void hk_on_kbm(void* obj, bool use, const void* method) {
    g_orig_on_kbm(obj, use, method);
    if (!g_seen_device_event || use != g_kbm)
        api->log(self, LAZARUS_LOG_INFO, "input device: %s (look scale %.2f)", use ? "mouse/keyboard" : "gamepad",
                 use ? g_mouse_scale : g_pad_scale);
    g_kbm = use;
    g_seen_device_event = true;
}

void on_update(void*) {
    ULONGLONG now = GetTickCount64();
    if (now - g_last_stats < 10000) return;
    g_last_stats = now;
    if (g_peak_raw > 0)
        api->log(self, LAZARUS_LOG_DEBUG, "look input peak over 10s: raw %.3f -> %.3f (%s)", g_peak_raw, g_peak_scaled,
                 g_kbm ? "mouse" : "gamepad");
    g_peak_raw = g_peak_scaled = 0;
}

float read_scale(const char* key) {
    double v = api->config_get_float(self, "look", key, 1.0);
    if (v < 0.05 || v > 10.0) {
        api->log(self, LAZARUS_LOG_WARN, "config.ini: %s = %g out of range (0.05-10), using 1.0", key, v);
        return 1.0f;
    }
    return (float)v;
}

}  // namespace

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    if (a->api_version < 2 || a->struct_size < sizeof(LazarusApi)) return 1;
    if (!il2::init(a)) {
        a->log(m, LAZARUS_LOG_ERROR, "GameAssembly.dll is missing %s", il2::missing());
        return 2;
    }
    g_mouse_scale = read_scale("mouse_scale");
    g_pad_scale = read_scale("gamepad_scale");
    g_y_ratio = read_scale("vertical_ratio");

    void* get_axis = a->method_pointer(
        a->find_method("Cinemachine.dll", "Cinemachine", "CinemachineInputProvider", "GetAxisValue", 1));
    if (!get_axis || a->hook_create(m, get_axis, (void*)&hk_get_axis, (void**)&g_orig_get_axis) != LAZARUS_OK) {
        a->log(m, LAZARUS_LOG_ERROR, "couldn't hook CinemachineInputProvider.GetAxisValue");
        return 3;
    }
    void* on_kbm = a->method_pointer(a->find_method("Lucille.BaseCharacter.dll", "Lucille.BaseCharacter",
                                                    "PlayerInputsReader", "OnKeyboardActive", 1));
    if (!on_kbm || a->hook_create(m, on_kbm, (void*)&hk_on_kbm, (void**)&g_orig_on_kbm) != LAZARUS_OK)
        a->log(m, LAZARUS_LOG_WARN, "couldn't hook OnKeyboardActive - using mouse_scale for every device");

    a->on_update(m, &on_update, nullptr);
    a->log(m, LAZARUS_LOG_INFO, "look scale: mouse %.2f, gamepad %.2f, vertical ratio %.2f", g_mouse_scale, g_pad_scale,
           g_y_ratio);
    return 0;
}
