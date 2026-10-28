// fake GameAssembly.dll. just enough il2cpp API for the loader: a few
// "assemblies", Internal_SceneLoaded, one gameplay method to hook, some
// icalls. the fake_* exports let the fake engine poke it

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cwchar>

#define EXPORT extern "C" __declspec(dllexport)

namespace {

struct FakeMethodInfo {
    void* methodPointer;  // has to be first, same as the real MethodInfo
    const char* name;
    int argc;
};
struct FakeClass {
    const char* ns;
    const char* name;
    FakeMethodInfo* methods;
    int count;
};
struct FakeImage {
    const char* name;
    FakeClass* classes;
    int count;
};
struct FakeAssembly {
    FakeImage* image;
};
struct FakeString {
    const wchar_t* chars;
};

volatile int g_init_called = 0;
volatile int g_shutdown_called = 0;
volatile int g_game_scene_listener_calls = 0;
volatile int g_target_frame_rate = -1;

// noinline and different bodies or the linker folds them, and minhook needs
// an actual function to patch
__declspec(noinline) void Internal_SceneLoaded(int32_t handle, int32_t mode, const void*) {
    g_game_scene_listener_calls = g_game_scene_listener_calls + 1;
    if (handle < 0 || mode < 0) OutputDebugStringA("impossible");
}

volatile int g_notify_calls = 0;
__declspec(noinline) void NotifyBeforeUpdate(int32_t type, const void*) {
    g_notify_calls = g_notify_calls + 1;
    if (type < 0) OutputDebugStringA("impossible");
}
volatile int g_frame = 0;

__declspec(noinline) int FakeGame_GetValue(const void*) {
    volatile int v = 1;
    return v;
}

FakeMethodInfo g_scene_methods[] = {{(void*)&Internal_SceneLoaded, "Internal_SceneLoaded", 2}};
FakeMethodInfo g_game_methods[] = {{(void*)&FakeGame_GetValue, "GetValue", 0}};
FakeMethodInfo g_input_methods[] = {{(void*)&NotifyBeforeUpdate, "NotifyBeforeUpdate", 1}};
FakeClass g_core_classes[] = {{"UnityEngine.SceneManagement", "SceneManager", g_scene_methods, 1}};
FakeClass g_game_classes[] = {{"Lucille.Fake", "FakeGame", g_game_methods, 1}};
FakeClass g_input_classes[] = {{"UnityEngineInternal.Input", "NativeInputSystem", g_input_methods, 1}};
FakeImage g_images[] = {{"UnityEngine.CoreModule.dll", g_core_classes, 1},
                        {"Lucille.Fake.dll", g_game_classes, 1},
                        {"UnityEngine.InputModule.dll", g_input_classes, 1}};
FakeAssembly g_assemblies[] = {{&g_images[0]}, {&g_images[1]}, {&g_images[2]}};
void* g_assembly_ptrs[] = {&g_assemblies[0], &g_assemblies[1], &g_assemblies[2]};
int g_domain = 0;

FakeString g_scene_names[] = {{L"Boot"}, {L"MainMenu"}, {L"Prison"}};

void* icall_GetNameInternal(int32_t handle) {
    return (handle >= 0 && handle < 3) ? &g_scene_names[handle] : nullptr;
}
int32_t icall_GetBuildIndexInternal(int32_t handle) { return handle; }
void icall_set_targetFrameRate(int32_t v) { g_target_frame_rate = v; }
int32_t icall_get_targetFrameRate() { return g_target_frame_rate; }

// roughly what a fresh unity player starts with
volatile int g_vsync = 1, g_quality = 1, g_aniso = 1;
volatile float g_lod = 1.0f;
volatile int g_res_w = 1920, g_res_h = 1080, g_res_mode = 1, g_res_hz = 0;

void icall_set_vsync(int32_t v) { g_vsync = v; }
int32_t icall_get_vsync() { return g_vsync; }
void icall_set_quality(int32_t q, bool) {
    // like real unity: switching preset stomps vsync/lod
    g_quality = q;
    g_vsync = 1;
    g_lod = 1.0f;
}
int32_t icall_get_quality() { return g_quality; }
void icall_set_lod(float v) { g_lod = v; }
float icall_get_lod() { return g_lod; }
void icall_set_aniso(int32_t v) { g_aniso = v; }
int32_t icall_get_aniso() { return g_aniso; }
void icall_set_resolution(int32_t w, int32_t h, int32_t mode, int32_t hz) {
    g_res_w = w;
    g_res_h = h;
    g_res_mode = mode;
    g_res_hz = hz;
}
int32_t icall_screen_w() { return g_res_w; }
int32_t icall_screen_h() { return g_res_h; }
int32_t icall_get_mode() { return g_res_mode; }
int32_t icall_frame_count() { return g_frame; }

// string[] laid out like a real Il2CppArray, items at +32
FakeString g_q_low{L"Low"}, g_q_med{L"Medium"}, g_q_high{L"High"};
struct {
    void* klass;
    void* monitor;
    void* bounds;
    size_t max_length;
    void* items[3];
} g_quality_names = {nullptr, nullptr, nullptr, 3, {&g_q_low, &g_q_med, &g_q_high}};
void* icall_get_names() { return &g_quality_names; }

struct IcallEntry {
    const char* name;
    void* fn;
};
const IcallEntry g_icalls[] = {
    {"UnityEngine.SceneManagement.Scene::GetNameInternal", (void*)&icall_GetNameInternal},
    {"UnityEngine.SceneManagement.Scene::GetBuildIndexInternal", (void*)&icall_GetBuildIndexInternal},
    {"UnityEngine.Application::set_targetFrameRate", (void*)&icall_set_targetFrameRate},
    {"UnityEngine.Application::get_targetFrameRate", (void*)&icall_get_targetFrameRate},
    {"UnityEngine.QualitySettings::set_vSyncCount", (void*)&icall_set_vsync},
    {"UnityEngine.QualitySettings::get_vSyncCount", (void*)&icall_get_vsync},
    {"UnityEngine.QualitySettings::SetQualityLevel", (void*)&icall_set_quality},
    {"UnityEngine.QualitySettings::GetQualityLevel", (void*)&icall_get_quality},
    {"UnityEngine.QualitySettings::get_names", (void*)&icall_get_names},
    {"UnityEngine.QualitySettings::set_lodBias", (void*)&icall_set_lod},
    {"UnityEngine.QualitySettings::get_lodBias", (void*)&icall_get_lod},
    {"UnityEngine.QualitySettings::set_anisotropicFiltering", (void*)&icall_set_aniso},
    {"UnityEngine.QualitySettings::get_anisotropicFiltering", (void*)&icall_get_aniso},
    {"UnityEngine.Screen::SetResolution", (void*)&icall_set_resolution},
    {"UnityEngine.Screen::get_width", (void*)&icall_screen_w},
    {"UnityEngine.Screen::get_height", (void*)&icall_screen_h},
    {"UnityEngine.Screen::get_fullScreenMode", (void*)&icall_get_mode},
    {"UnityEngine.Time::get_frameCount", (void*)&icall_frame_count},
};

}  // namespace

EXPORT int il2cpp_init(const char*) {
    g_init_called = 1;
    return 1;
}
EXPORT void il2cpp_shutdown() { g_shutdown_called = 1; }
EXPORT void* il2cpp_domain_get() { return &g_domain; }
EXPORT void** il2cpp_domain_get_assemblies(void*, size_t* size) {
    *size = 3;
    return g_assembly_ptrs;
}
EXPORT void* il2cpp_assembly_get_image(void* a) { return static_cast<FakeAssembly*>(a)->image; }
EXPORT const char* il2cpp_image_get_name(void* i) { return static_cast<FakeImage*>(i)->name; }
EXPORT void* il2cpp_class_from_name(void* i, const char* ns, const char* name) {
    auto img = static_cast<FakeImage*>(i);
    for (int c = 0; c < img->count; ++c)
        if (!std::strcmp(img->classes[c].ns, ns) && !std::strcmp(img->classes[c].name, name)) return &img->classes[c];
    return nullptr;
}
EXPORT void* il2cpp_class_get_method_from_name(void* k, const char* name, int argc) {
    auto klass = static_cast<FakeClass*>(k);
    for (int m = 0; m < klass->count; ++m)
        if (!std::strcmp(klass->methods[m].name, name) && (argc < 0 || klass->methods[m].argc == argc))
            return &klass->methods[m];
    return nullptr;
}
EXPORT void* il2cpp_resolve_icall(const char* name) {
    for (auto& e : g_icalls)
        if (!std::strcmp(name, e.name)) return e.fn;
    return nullptr;
}
EXPORT uint32_t il2cpp_array_length(void* arr) {
    return (uint32_t) reinterpret_cast<size_t*>(static_cast<char*>(arr) + 24)[0];
}
EXPORT const wchar_t* il2cpp_string_chars(void* s) { return static_cast<FakeString*>(s)->chars; }
EXPORT int32_t il2cpp_string_length(void* s) { return (int32_t)std::wcslen(static_cast<FakeString*>(s)->chars); }

// ---- the fake engine calls these ----

// goes through methodPointer like the real engine does, so the hook gets hit
EXPORT void fake_engine_load_scene(int32_t handle, int32_t mode) {
    auto fn = reinterpret_cast<void (*)(int32_t, int32_t, const void*)>(g_scene_methods[0].methodPointer);
    fn(handle, mode, &g_scene_methods[0]);
}
EXPORT int fake_game_get_value() {
    auto fn = reinterpret_cast<int (*)(const void*)>(g_game_methods[0].methodPointer);
    return fn(&g_game_methods[0]);
}
// one frame. input pumps twice per frame, that's the frameCount gating test
EXPORT void fake_engine_frame() {
    g_frame = g_frame + 1;
    auto fn = reinterpret_cast<void (*)(int32_t, const void*)>(g_input_methods[0].methodPointer);
    fn(1, &g_input_methods[0]);
    fn(4, &g_input_methods[0]);
}

// the game resetting stuff between scenes
EXPORT void fake_game_meddle() {
    g_target_frame_rate = 30;
    g_vsync = 1;
}
EXPORT int fake_state(const char* what) {
    if (!std::strcmp(what, "init")) return g_init_called;
    if (!std::strcmp(what, "shutdown")) return g_shutdown_called;
    if (!std::strcmp(what, "scene_listener")) return g_game_scene_listener_calls;
    if (!std::strcmp(what, "target_frame_rate")) return g_target_frame_rate;
    if (!std::strcmp(what, "vsync")) return g_vsync;
    if (!std::strcmp(what, "quality")) return g_quality;
    if (!std::strcmp(what, "lod_x100")) return (int)(g_lod * 100.0f + 0.5f);
    if (!std::strcmp(what, "aniso")) return g_aniso;
    if (!std::strcmp(what, "res_w")) return g_res_w;
    if (!std::strcmp(what, "res_h")) return g_res_h;
    if (!std::strcmp(what, "res_mode")) return g_res_mode;
    if (!std::strcmp(what, "res_hz")) return g_res_hz;
    if (!std::strcmp(what, "notify_calls")) return g_notify_calls;
    return -999;
}
