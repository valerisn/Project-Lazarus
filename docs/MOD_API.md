# Mod API (v2)

Mods are native x64 DLLs written against one header, [`include/lazarus/lazarus_api.h`](../include/lazarus/lazarus_api.h). It's plain C, so any compiler or CRT works. The smallest working mod is [`examples/hello-mod`](../examples/hello-mod).

## Layout

```
<game>/Lazarus/mods/<id>/
    mod.ini          required manifest
    <name>.dll       your code
    config.ini       optional, your settings, read via config_get_*
```

### mod.ini

| key | required | meaning |
|---|---|---|
| `id` | yes | lowercase letters, digits, `- _ .`, max 64 chars. Should match the folder name. |
| `dll` | yes | bare file name inside the mod folder. Paths and `..` are rejected. |
| `api_version` | yes | the `LAZARUS_API_VERSION` you built against. A loader older than this skips the mod. |
| `name`, `version`, `author`, `description` | no | shown in the log |
| `game_builds` | no | `any` (default) or comma-separated build fingerprints. Pin a mod when it hooks game methods whose layout can change between patches. |
| `requires` | no | comma-separated mod ids that must load first |

A mod only loads when its id appears in `[mods] enabled` in `Lazarus/lazarus.ini`. Dropping a folder in is never enough on its own.

## Entry points

```c
__declspec(dllexport) int  lazarus_mod_init(LazarusMod* self, const LazarusApi* api);  // 0 = success
__declspec(dllexport) void lazarus_mod_shutdown(LazarusMod* self);                     // optional
```

`lazarus_mod_init` runs on the main thread right after IL2CPP initializes, before the first scene loads. Engine objects don't exist yet, so do setup and hooking here and engine calls in `on_scene_loaded`.

Returning non-zero, or raising an exception, disables the mod for the session. The loader removes any hooks and callbacks it registered. If the process dies during init, the mod is quarantined on the next launch (`Lazarus/state/quarantine.txt`).

## Functions

| function | notes |
|---|---|
| `log(self, level, fmt, ...)` | printf-style, written to `lazarus.log` tagged `[mod:<id>]` |
| `config_get_string/int/float/bool` | read `config.ini`. Malformed values return your default and log a warning. |
| `mod_dir(self)` | absolute UTF-8 path of your mod folder |
| `il2cpp_export(name)` | any `il2cpp_*` export from GameAssembly.dll |
| `resolve_icall(name)` | Unity native call by `Namespace.Class::method`, no signature. Call it with the managed parameters, no MethodInfo. |
| `find_method(assembly, ns, class, method, argc)` | returns a `MethodInfo*`. `argc = -1` takes the first overload. |
| `method_pointer(mi)` | native address of the method body, the thing you hook |
| `hook_create(self, target, detour, &original)` | inline hook (MinHook), enabled immediately, removed automatically at shutdown |
| `hook_remove(self, target)` | removes one of your hooks early |
| `on_scene_loaded(self, fn, user)` | `fn(user, scene_name, build_index, mode)` after every scene load, after the game's own listeners run |

## API v2 additions

Check `api->api_version >= 2` before using these. A v1 mod (`api_version = 1` in mod.ini) keeps working unchanged.

| function | notes |
|---|---|
| `on_update(self, fn, user)` | `fn(user)` once per frame on the main thread. Frame-gated on `Time.frameCount`, because the engine hooks behind it fire several times per frame. |
| `run_on_main(self, fn, user)` | **thread-safe**. Queues `fn(user)` for the next frame on the main thread. Use it to touch game state from your own threads (UI, timers). |
| `register_command(self, name, help, fn, user)` | adds a console command. `fn(user, argc, argv, print, print_ctx)` runs on the main thread. Names are case-insensitive; you can't replace another mod's command. |
| `execute_command(line, print, ctx)` | run a command line (main thread only). Returns `LAZARUS_ERR_NOT_FOUND` for unknown commands. |

The per-frame tick comes from hooking `UnityEngineInternal.Input.NativeInputSystem.NotifyBeforeUpdate` and `UnityEngine.BeforeRenderHelper.Invoke`, the two native-to-managed calls the engine makes every frame in this build. Crashing update callbacks, tasks, and commands are caught and disabled, just like scene callbacks.

## IL2CPP calling convention

The native signature of a managed method is:

* static: `ret fn(args..., const MethodInfo*)`
* instance: `ret fn(Il2CppObject* this, args..., const MethodInfo*)`
* value-type structs with a single int field (such as `Scene`) are passed as that int

Icalls (`resolve_icall`) take only the managed arguments, with no MethodInfo.

## Finding things to hook

```
python tools/il2cpp_meta.py "<game>/The Walking Dead Destinies_Data/il2cpp_data/Metadata/global-metadata.dat" --grep Sensitivity
```

This prints image, namespace, class, method names and parameter names. Keep dumps in `local/` (gitignored). Don't commit them.

Many Unity engine methods the game never calls were stripped from the managed side (for example `Application.targetFrameRate`). Their icalls still exist in `UnityPlayer.dll`, so `resolve_icall` is the way to reach them.

## Compatibility promise

Fields are only ever appended to `LazarusApi`. A mod built against v1 keeps working on later loaders. If you use a field added later, check `api->struct_size` first.
