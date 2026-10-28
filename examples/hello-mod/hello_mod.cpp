// minimal mod: logs scene loads and reads one setting. copy this folder to start your own

#include "lazarus/lazarus_api.h"

static const LazarusApi* api;
static LazarusMod* self;

static void on_scene(void*, const char* name, int build_index, int mode) {
    if (api->config_get_bool(self, "hello", "log_scenes", 1))
        api->log(self, LAZARUS_LOG_INFO, "hello from scene '%s' (index %d, %s)", name, build_index,
                 mode ? "additive" : "single");
}

// hooking a game method looks like this (left out because it changes gameplay):
//
//   static void (*orig_apply)(void* self_obj, const void* method);
//   static void hk_apply(void* self_obj, const void* method) {
//       api->log(self, LAZARUS_LOG_DEBUG, "camera sensitivity applied");
//       orig_apply(self_obj, method);
//   }
//
// and in init:
//   void* mi = api->find_method("Lucille.CameraSystem.dll", "Lucille.CameraSystem",
//                               "CameraSensitivity", "ApplySensitivity", 0);
//   api->hook_create(self, api->method_pointer(mi), (void*)&hk_apply, (void**)&orig_apply);
//
// il2cpp: instance methods get `this` first, everything gets a trailing
// MethodInfo*. tools/il2cpp_meta.py finds names and arg counts

extern "C" __declspec(dllexport) int lazarus_mod_init(LazarusMod* m, const LazarusApi* a) {
    api = a;
    self = m;
    api->log(self, LAZARUS_LOG_INFO, "hello-mod loaded on loader %s, game build %s%s", api->loader_version,
             api->game_build, api->game_build_known ? "" : " (untested)");
    return api->on_scene_loaded(self, &on_scene, nullptr);
}
