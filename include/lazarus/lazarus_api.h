/*
 * Project Lazarus mod API.
 *
 * Plain C on purpose: mods can be built with any compiler/CRT and we never
 * pass C++ objects across the DLL boundary. Include this header and nothing
 * else from the loader.
 *
 * A mod is a DLL that exports:
 *
 *   int  lazarus_mod_init(LazarusMod* self, const LazarusApi* api);   // required, return 0 on success
 *   void lazarus_mod_shutdown(LazarusMod* self);                      // optional
 *
 * Threading: init, shutdown and every event callback run on the game's main
 * thread. Don't block in them.
 *
 * Lifetime: `api` and `self` stay valid until lazarus_mod_shutdown returns.
 * Hooks and event registrations made by a mod are removed by the loader if the
 * mod fails to init, and on shutdown, so a mod doesn't have to track them.
 *
 * Compatibility rule for future versions: fields are only ever appended to
 * LazarusApi. Check api->struct_size before touching anything newer than
 * what your mod was built against.
 */
#ifndef LAZARUS_API_H
#define LAZARUS_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAZARUS_API_VERSION 2

#define LAZARUS_OK 0
#define LAZARUS_ERR_INVALID_ARG 1
#define LAZARUS_ERR_NOT_FOUND 2
#define LAZARUS_ERR_HOOK_FAILED 3
#define LAZARUS_ERR_NOT_READY 4

typedef enum LazarusLogLevel {
    LAZARUS_LOG_TRACE = 0,
    LAZARUS_LOG_DEBUG = 1,
    LAZARUS_LOG_INFO = 2,
    LAZARUS_LOG_WARN = 3,
    LAZARUS_LOG_ERROR = 4
} LazarusLogLevel;

/* opaque, one per loaded mod */
typedef struct LazarusMod LazarusMod;

/* scene_name is UTF-8 and only valid during the call.
 * mode: 0 = Single, 1 = Additive (UnityEngine.SceneManagement.LoadSceneMode). */
typedef void (*LazarusSceneLoadedFn)(void* user, const char* scene_name, int build_index, int mode);

/* v2 */
typedef void (*LazarusTaskFn)(void* user);
typedef void (*LazarusPrintFn)(void* ctx, const char* text);
/* argv[0] is the command name. Print output with print(print_ctx, "..."). */
typedef void (*LazarusCommandFn)(void* user, int argc, const char** argv, LazarusPrintFn print, void* print_ctx);

typedef struct LazarusApi {
    uint32_t struct_size; /* sizeof(LazarusApi) as compiled into the loader */
    uint32_t api_version; /* LAZARUS_API_VERSION of the loader */
    const char* loader_version;
    const char* game_build;   /* 16 hex chars, "" if unknown */
    int game_build_known;     /* 1 if game_build is a build Lazarus has been checked against */

    /* ---- logging: goes to Lazarus/logs/lazarus.log tagged with the mod id ---- */
    void (*log)(LazarusMod* self, LazarusLogLevel level, const char* fmt, ...);

    /* ---- per-mod settings: mods/<id>/config.ini ----
     * Getters return the default when the key is missing or malformed (and log a
     * warning for malformed). config_get_string copies into buf (always
     * NUL-terminated) and returns 1 if the key existed. */
    int (*config_get_string)(LazarusMod* self, const char* section, const char* key, char* buf, size_t buf_len);
    int64_t (*config_get_int)(LazarusMod* self, const char* section, const char* key, int64_t def);
    double (*config_get_float)(LazarusMod* self, const char* section, const char* key, double def);
    int (*config_get_bool)(LazarusMod* self, const char* section, const char* key, int def);
    const char* (*mod_dir)(LazarusMod* self); /* UTF-8 absolute path of mods/<id> */

    /* ---- IL2CPP access ----
     * il2cpp_export: GetProcAddress on GameAssembly.dll ("il2cpp_string_new", ...).
     * resolve_icall: Unity internal call by "Namespace.Class::method", e.g.
     *   "UnityEngine.Application::set_targetFrameRate". NULL if unknown.
     * find_method: MethodInfo* for a managed method, NULL if not found.
     *   assembly is the image name, e.g. "Lucille.CameraSystem.dll".
     *   param_count -1 = first overload with that name.
     * method_pointer: native code address of a MethodInfo (what you hook). */
    void* (*il2cpp_export)(const char* name);
    void* (*resolve_icall)(const char* name);
    void* (*find_method)(const char* assembly, const char* name_space, const char* klass, const char* method,
                         int param_count);
    void* (*method_pointer)(void* method_info);

    /* ---- hooks (MinHook under the hood) ----
     * hook_create installs and enables immediately. *original receives a
     * callable trampoline to the original function. */
    int (*hook_create)(LazarusMod* self, void* target, void* detour, void** original);
    int (*hook_remove)(LazarusMod* self, void* target);

    /* ---- events ----
     * Fires after every Unity scene load, including the first. This is the
     * earliest point where calling engine APIs is safe. Exceptions thrown out
     * of a callback disable that callback, the game keeps running. */
    int (*on_scene_loaded)(LazarusMod* self, LazarusSceneLoadedFn fn, void* user);

    /* ---- v2 (check struct_size / api_version >= 2 before use) ---- */

    /* Called once per frame on the main thread (gated on Time.frameCount).
     * Keep it cheap. Returns LAZARUS_ERR_NOT_READY if the loader couldn't
     * find a per-frame engine callback to hook. */
    int (*on_update)(LazarusMod* self, LazarusTaskFn fn, void* user);

    /* Thread-safe: queue fn to run on the main thread at the next frame.
     * This is the only safe way to touch game/engine state from your own
     * threads (UI threads, timers, etc.). */
    int (*run_on_main)(LazarusMod* self, LazarusTaskFn fn, void* user);

    /* Console commands. Names: letters, digits, _ . - (case-insensitive).
     * Handlers run on the main thread. A command registered by another mod
     * can't be replaced (LAZARUS_ERR_INVALID_ARG). */
    int (*register_command)(LazarusMod* self, const char* name, const char* help, LazarusCommandFn fn, void* user);

    /* Main thread only (use run_on_main). Returns LAZARUS_OK, or
     * LAZARUS_ERR_NOT_FOUND for an unknown command (a hint gets printed). */
    int (*execute_command)(const char* line, LazarusPrintFn print, void* print_ctx);
} LazarusApi;

#ifdef __cplusplus
}
#endif

#endif /* LAZARUS_API_H */
