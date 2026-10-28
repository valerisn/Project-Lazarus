# Testing

```
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

configures, builds, and runs all three suites through ctest. CI runs the same thing on `windows-2022` for every push.

## Suites

| suite | what it covers |
|---|---|
| `core_unit_tests` | INI parsing, logging and rotation, manifest validation (including path traversal), config defaults and bad values, discovery, load planning (enable list, dependencies, cycles, API version, build pinning, quarantine), crash lock, fingerprinting, graphics-settings config parsing |
| `installer_tests` (Pester 3/4) | missing-file detection, Mono/untested-build warnings, install, uninstall back to a byte-identical folder, foreign `version.dll` backup/restore, settings preserved on reinstall, refusal to delete a replaced `version.dll`, interrupted install, Steam library discovery |
| `e2e_bootstrap` | the real `version.dll` proxy inside a fake Unity process (see below), 14 scenarios |

### End-to-end harness

`tests/e2e` builds a folder that mirrors the game's structure. `fake_game.exe` imports `UnityPlayer.dll`, which imports `VERSION.dll` and resolves `il2cpp_init` from `GameAssembly.dll` through its own `GetProcAddress` import, the same shape as the real engine (confirmed with `dumpbin /imports` against the shipped `UnityPlayer.dll`). The real proxy is copied in as `version.dll`. Each scenario is a fresh process:

happy path (forwarding, interception, hooks, scene dispatch, icalls, clean shutdown), mod not enabled, safe mode, loader disabled, init returns an error, init throws, an access violation in a scene callback, a hard process kill during init, quarantine on the next launch plus un-quarantine, a corrupted manifest beside a good mod, a missing `lazarus.ini`, and three graphics-settings scenarios (applies config and survives a quality-preset reset and the game resetting values between scenes; an empty config changes nothing; bad values are rejected while good ones apply).

## What this does and does not prove

**Verified locally:** everything above, plus static checks against the shipped game files: the import tables, the absence of delay-loading, the il2cpp exports, the metadata version, the icall names, and that the installer finds and validates the real install on a custom drive path.

## Live results

### Run 1: 2026-10-28, build 12839205 (`1b579b75b1486c0e`), Unity 2021.3.17f1

Config: graphics-settings with `target_fps = 45`, `vsync = 0`; fps-counter with default settings; `log_level = debug`. The game was installed with `Install-Lazarus.ps1` on a custom drive path.

| check | expected | observed |
|---|---|---|
| proxy boots, `il2cpp_init` intercepted | `intercepted il2cpp_init` | yes |
| build recognized | known build line | `Steam build 12839205 (December 2023 release)` |
| scene hook fires in the real engine | `scene loaded:` lines | `Startup` (mode 2), `Splashscreen`, `Initialize`, `Main Menu` |
| icalls registered, settings applied | read-back lines | `vsync set to 0`, `frame rate cap set to 45`, no drift warnings on later scenes |
| frame cap effective | ~45 fps | log averages 43.5 / 40.5 / 44.4 fps per 10 s window (dips during scene loads); overlay showed 46 (see below) |
| DXGI Present hook | frames counted | D3D11 Present + Present1 hooked, 5320 frames in the session |
| `il2cpp_shutdown` called on quit | `shutdown complete` | yes |
| safe mode (SHIFT), uninstall | | not tested yet |

Findings from this run:

* The overlay showed 46 instead of 45. Cause: it counted whole frames per 0.5 s window (22 or 23 at 45 fps). Fixed by computing FPS from QPC frame timestamps.
* After the game exited, a non-Unity process (most likely `UnityCrashHandler64.exe`) loaded the proxy and truncated `bootstrap.log`. Fixed: the proxy no longer writes any file outside a Unity process. Covered by an e2e regression check.
* The first 10 s window averaged **368 fps**, even though the cap was applied at the first scene (`Startup`). Cause not yet known. It covers the Unity splash/startup phase. Later windows are capped.
* The game ships a single quality level (`0=Ultra`), so `quality_level` has no effect in this game.
* Initial scene load mode is reported as 2 (an internal value, not Single/Additive). Additive-only re-apply logic is unaffected.

### Runs 2-3: 2026-10-28, same build: skip-intro and dev console

* **skip-intro, verified in game.** Splashscreen to Initialize took **4.1 s** (vanilla 21.3 s). The splash census shows two `VideoPlayer`s (GameMill and Flux logos) driven by `Lucille.BootupVideoSequence`. The first attempt (11.7 s) only looked like it failed: `Initialize` took 9 s to load cold, with the logos still on screen. On the second run it loaded in 1.6 s.
* **dev-console, opens in game.** F6 opens it; `help` lists all commands. The level/cheat commands haven't been exercised yet.
* `LevelSelection` (the developers' level-select scene) loads from the normal main-menu flow.

## Still unverified

1. Safe mode via SHIFT and uninstall on the real install.
2. Which `QualitySettings` (LOD bias, anisotropic) have a visible effect under HDRP (issue #6).
3. Window mode and resolution changes in the real game.

## Live verification procedure

Game: The Walking Dead: Destinies, Steam build 12839205 (fingerprint `1b579b75b1486c0e`), Unity 2021.3.17f1.

1. Install with `Install.bat`.
2. In `Lazarus\mods\graphics-settings\config.ini` set `target_fps = 45` and `vsync = 0`. 45 is an unusual value, so it's easy to confirm.
3. Start the game, reach the main menu, open an FPS overlay (Steam overlay → in-game FPS counter).
4. Expected: about 45 FPS. `lazarus.log` contains `scene load hook installed`, `scene loaded: '<name>'` lines, `[mod:graphics-settings] frame rate cap set to 45`, and `startup complete: 1 mod(s) active`.
5. Quit normally. Expected: `shutdown complete` at the end of the log.
6. Hold SHIFT while starting. Expected: `safe mode: SHIFT held during startup`, normal frame rate.
7. Run `Uninstall.bat`. Expected: game folder back to stock.

Please attach `lazarus.log` and `bootstrap.log` when reporting results.
