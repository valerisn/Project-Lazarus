# Changelog

Notable changes to Project Lazarus. Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions will follow [Semantic Versioning](https://semver.org/) once releases start.

"Verified in game" means it was observed working in The Walking Dead: Destinies, Steam build 12839205. Everything else is tested against the simulated engine only.

## [Unreleased]

Nothing is tagged yet. Everything below is on `main` as version 0.1.0.

### Loader

- Custom native loader shipped as a `version.dll` proxy. It forwards all 17 exports to the system DLL, intercepts `il2cpp_init`/`il2cpp_shutdown` through Unity's import table, and hooks scene loads. **Verified in game.**
- Mod discovery, explicit opt-in enable list, dependency ordering, cycle detection, API version checks and game build pinning by `GameAssembly.dll` fingerprint.
- Safe mode (SHIFT at startup, `LAZARUS_SAFE_MODE=1`, or config), automatic crash quarantine via a boot lock, and SEH guards around every call into mod code.
- Mod API v2: per-frame `on_update`, thread-safe `run_on_main`, and console command registration and execution. v1 mods keep working.
- Built-in console commands `help` and `mods`.

### Mods

- **graphics-settings**: frame cap, vsync, window mode, resolution, LOD bias and anisotropic filtering, set through Unity engine calls with read-back and re-apply on scene load. Frame cap and vsync **verified in game**. Also a watchdog that re-checks the cap every 30 frames (#8).
- **fps-counter**: DXGI `Present` hook with a click-through overlay and log averages. **Verified in game** (D3D11).
- **skip-intro**: skips the logo videos by loading the game's own `Initialize` collection, with a timescale fallback and a scene census. **Verified in game**: 21.3 s down to 4.1 s.
- **dev-console**: F6 console with level loading through the game's `SceneCollection`s, access to the developers' shipped cheat system (`Lucille.LucilleCheats`), object, class and scene inspection, and autoexec. Opens in game; level and cheat commands not yet verified.
- **mouse-sensitivity** (experimental, #2): per-device look scale through `CinemachineInputProvider.GetAxisValue`.
- **key-rebind** (experimental, #3): config-file rebinding through Input System binding overrides, plus a JSON dump of the game's actions.
- **hello-mod** example.

### Installer

- `Install-Lazarus.ps1` with `Install.bat` / `Uninstall.bat`: Steam library and custom-path discovery, game and build validation, backup of any existing `version.dll`, hash-checked manifest uninstall, recovery from interrupted installs, and user settings preserved on reinstall.

### Tools

- `tools/il2cpp_meta.py`: IL2CPP metadata v29 reader for finding classes and methods, with nested types shown as `Outer/Inner` (#7).

### Fixed

- The FPS overlay showed 46 at a 45 cap (frame-count quantization). It now measures from QPC frame timestamps.
- A non-Unity process in the game folder (crash handler) wiped `bootstrap.log` on exit.
- The installer failed when started from PowerShell 7 (`Get-FileHash` unavailable). It now hashes with .NET.
- Drive search for the game took 303 s. It now takes 8 s: secondary drives first, huge system folders skipped.
- A worker-thread race in an end-to-end test.

### Docs and project

- README, install/recovery, console, mod API, architecture, feasibility, research, testing, contributing, security policy and code of conduct; issue and PR templates; CI on `windows-2022`.
