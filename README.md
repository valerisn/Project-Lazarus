# Project Lazarus

A community restoration project for **The Walking Dead: Destinies** (Steam, PC, App ID 2192790), built on its own custom mod loader.

[![build](https://github.com/valerisn/Project-Lazarus/actions/workflows/build.yml/badge.svg)](https://github.com/valerisn/Project-Lazarus/actions/workflows/build.yml)

> **Status: pre-release. The loader and the graphics-settings fix have been verified in the real game** (build 12839205, first live run 2026-10-28: loader boots, scene hook fires, 45 FPS cap holds, clean shutdown). Details are in [docs/TESTING.md](docs/TESTING.md#live-results). Safe mode, uninstall on a real install, and most graphics options are still unverified in game.

## Why

The PC release shipped with almost nothing configurable. The only graphics option is Film Grain: no frame cap, no vsync toggle, no window mode, no resolution, no quality levels. There's also no key rebinding, and players report a couple of progression blockers. The developers haven't patched it since. There is no existing modding scene, mod loader, or Nexus page for the game.

The game is Unity 2021.3.17f1 using **IL2CPP**, so ordinary .NET mods and Harmony don't apply. Project Lazarus ships its own small native loader rather than depending on BepInEx or MelonLoader.

## Current status

| component | state |
|---|---|
| Custom loader (`version.dll` proxy + IL2CPP runtime) | **verified in game**: boots, hooks scene loads, loads mods, shuts down cleanly |
| Mod API v1 (C ABI) | done, documented in [docs/MOD_API.md](docs/MOD_API.md) |
| Safe mode, crash quarantine, build fingerprinting | done, tested |
| **graphics-settings** fix: frame cap, vsync, window mode, resolution, LOD bias, anisotropic filtering | frame cap + vsync **verified in game**. Other options built and tested against the fake engine, not yet checked in game. The game has only one quality level, so `quality_level` does nothing here. |
| **fps-counter**: on-screen FPS box and FPS in the log | **verified in game** (D3D11) |
| **dev-console** (F6): jump to any level, the game's own developer cheats (god mode, skip objective, end level...), object inspection, autoexec | built and tested against the fake engine. **Game commands not yet run in game.** See [docs/CONSOLE.md](docs/CONSOLE.md) |
| Installer / uninstaller with Steam library discovery | done, tested. Discovery confirmed against a real install on a custom drive path. |
| Mouse sensitivity fix | planned, see issues |
| Key rebinding | planned, see issues |
| Governor finale softlocks | investigating. Located in `23_WGCFacilityTooFarGone`; `load TooFarGone` should replace the need for save files |

**Supported game version:** Steam build 12839205 (December 2023, GameAssembly fingerprint `1b579b75b1486c0e`). As far as we can tell this is the only PC build that ever shipped. On other builds the loader still runs but skips mods pinned to a specific build.

## Install

Grab `project-lazarus-dist` from the latest successful [CI run](https://github.com/valerisn/Project-Lazarus/actions) (proper releases come after live verification), close the game, and run `Install.bat`. It finds the game in any Steam library or custom folder, asks before changing anything, and only *adds* `version.dll` and a `Lazarus` folder. No original game file is touched.

Then edit `Lazarus\mods\graphics-settings\config.ini`. Every option is blank (meaning "leave alone") by default.

Full details: [docs/INSTALL.md](docs/INSTALL.md).

## Uninstall

Run `Uninstall.bat`, or delete `version.dll` and the `Lazarus` folder from the game folder. That's everything Lazarus adds.

## Logs and recovery

* Log: `<game>\Lazarus\logs\lazarus.log` (previous session in `lazarus.prev.log`)
* Hold **SHIFT** while the game starts: safe mode, no mods load
* A mod that crashes the game during startup is quarantined automatically on the next launch

## Building

Requirements: Windows, Visual Studio 2022 (C++ desktop workload, includes MASM), CMake 3.20 or newer.

```bash
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
```

This configures, builds, and runs all tests. The installable layout ends up in `build/dist/`. The only third-party code is MinHook, which is vendored, so no network access is needed. See [docs/TESTING.md](docs/TESTING.md).

## Reporting bugs

Open an issue with your `lazarus.log`, `bootstrap.log` if present, what you did, what you expected, and what happened. For game bugs (not Lazarus bugs), say where in the game it happens and whether it reproduces. A save file right before the problem helps a lot.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The quickest way in is writing a mod: [docs/MOD_API.md](docs/MOD_API.md) and [examples/hello-mod](examples/hello-mod).

## Documentation

* [docs/FEASIBILITY.md](docs/FEASIBILITY.md): build facts and why the loader is designed the way it is
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): boot sequence, lifecycle, code layout
* [docs/RESEARCH.md](docs/RESEARCH.md): community-reported issues, evidence, and feasibility
* [docs/MOD_API.md](docs/MOD_API.md), [docs/INSTALL.md](docs/INSTALL.md), [docs/TESTING.md](docs/TESTING.md)

## License

MIT for Project Lazarus code. MinHook is BSD-2-Clause; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This repository contains no game files, binaries, metadata, or assets. You need your own copy of the game.
