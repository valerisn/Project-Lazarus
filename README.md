# Project Lazarus

**A community restoration project for The Walking Dead: Destinies on PC, built on its own mod loader.**

[![build](https://github.com/valerisn/Project-Lazarus/actions/workflows/build.yml/badge.svg)](https://github.com/valerisn/Project-Lazarus/actions/workflows/build.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)
![game build: 12839205](https://img.shields.io/badge/game%20build-12839205-orange.svg)

The PC release of *The Walking Dead: Destinies* (Steam App ID 2192790) shipped with a Film Grain toggle as its only graphics option. It has no frame cap, vsync, window mode or key rebinding, mouse aim that players call unusable, and a couple of reported softlocks. It never got a fix, and it has no modding scene. Project Lazarus is a loader and a set of fixes that work around all of that without modifying a single original game file.

> **Status: pre-release, in active development.** The loader and three mods are verified in the real game. Others are built and tested against a simulated engine but haven't been tried in game yet. Everything below says which is which. Test records live in [docs/TESTING.md](docs/TESTING.md).

## What you get

### Verified in the real game

| mod | what it does |
|---|---|
| **graphics-settings** | Frame rate cap and vsync, re-applied if the game resets them. Also window mode, resolution, LOD bias and anisotropic filtering (those four are not yet checked in game). |
| **skip-intro** | Skips the publisher/developer logo videos. Boot to the main menu went from 21.3 s to 4.1 s. |
| **fps-counter** | Small click-through FPS readout over the game, plus averages in the log. |

### Built, waiting for an in-game test

| mod | what it does |
|---|---|
| **dev-console** | Press **F6** for a console. Jump to any level (`load TooFarGone`), use the developers' own cheats that shipped in the game (god mode, skip objective, end level...), inspect game objects, autoexec at startup. See [docs/CONSOLE.md](docs/CONSOLE.md). The console opens in game; level and cheat commands are untested. |
| **mouse-sensitivity** | Separate look-speed scale for mouse and controller, on top of the in-game slider. *Experimental.* |
| **key-rebind** | Rebind any control from a config file. Writes the game's full action list to JSON so you know what to type. *Experimental.* |

### Under investigation

* Governor finale softlocks (tank scene freeze, Daryl vs Governor fight): [#4](https://github.com/valerisn/Project-Lazarus/issues/4)
* Frame cap not holding during the first seconds of boot: [#8](https://github.com/valerisn/Project-Lazarus/issues/8)

## Quick start

1. **Download** `project-lazarus-dist` from the latest green [build run](https://github.com/valerisn/Project-Lazarus/actions/workflows/build.yml). Versioned releases will follow once more of the above is verified.
2. **Close the game** and run `Install.bat`. It finds the game in any Steam library or custom folder, shows you what it found, and asks before changing anything.
3. **Pick your mods** in `<game>\Lazarus\lazarus.ini`:
   ```ini
   [mods]
   enabled = graphics-settings, skip-intro, fps-counter
   ```
   A mod only runs if it's listed here. That's deliberate; see [Safety](#safety).
4. **Configure** each mod in `<game>\Lazarus\mods\<mod>\config.ini`. Every setting starts at "leave the game alone".
5. Start the game normally through Steam.

Full guide: [docs/INSTALL.md](docs/INSTALL.md).

## Uninstall

Run `Uninstall.bat`. It removes exactly what was installed, using the record it made at install time, and restores anything it backed up. Doing it by hand also works: delete `version.dll` and the `Lazarus` folder from the game folder. Those are the only two things Lazarus ever adds.

## Safety

* **No original game file is modified.** Installing adds `version.dll` and a `Lazarus\` folder.
* **Mods are opt-in.** Dropping a mod into the folder never runs it; you have to list it in `lazarus.ini`.
* **Safe mode.** Hold **SHIFT** while the game starts and no mods load.
* **Crash quarantine.** If a mod crashes the game while starting, it's disabled automatically on the next launch, with a note in `Lazarus\state\quarantine.txt`.
* **Fails open.** If anything in the loader goes wrong, it logs the problem and the game runs unmodified.
* **No network access, no auto-updates, no admin rights.**

Mods are native code and run with your user's permissions, like any program. Only enable mods you trust. Details and how to report problems: [SECURITY.md](SECURITY.md).

## Logs

Everything goes to `<game>\Lazarus\logs\lazarus.log`, including every line the dev console prints. The previous session is kept as `lazarus.prev.log`. `Install-Lazarus.ps1 -Status` shows the install state and the end of the log.

## Compatibility

* **Game:** Steam build 12839205 (December 2023), GameAssembly fingerprint `1b579b75b1486c0e`. As far as we can tell, that's the only PC build that ever shipped. On any other build the loader still runs, but mods that hook game code are skipped until they're verified for it.
* **Engine:** Unity 2021.3.17f1, IL2CPP, HDRP. DirectX 11 is confirmed in game.
* **OS:** Windows 10/11 x64.
* **Not supported:** exclusive fullscreen hides the FPS and console overlays (borderless and windowed are fine).

## How it works

The game is IL2CPP: its C# was compiled to native code, so .NET mod frameworks and Harmony don't apply. Instead of pulling in BepInEx or MelonLoader, Lazarus is a small native loader of its own:

1. `version.dll` in the game folder is loaded by Unity's engine DLL and forwards every call to the real Windows one.
2. It intercepts `il2cpp_init` and starts once the game's managed runtime is up.
3. Mods are plain C ABI DLLs. They get logging, config, IL2CPP lookups, function hooking (MinHook), scene and frame callbacks, a main-thread task queue and console commands.

More: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/FEASIBILITY.md](docs/FEASIBILITY.md).

## Building

Requirements: Windows, Visual Studio 2022 with the C++ desktop workload (includes MASM), CMake 3.20 or newer. Python 3 is optional and only used for the tool tests.

```bash
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
```

This configures, builds, and runs every test suite: 50 unit tests, 15 installer tests, 4 tool tests, and 16 end-to-end scenarios. The end-to-end scenarios boot the real loader inside a fake Unity process laid out like the game, including real D3D11 frames. The installable layout ends up in `build/dist/`. CI runs the same thing on every push. Details: [docs/TESTING.md](docs/TESTING.md).

## Writing a mod

Start from [examples/hello-mod](examples/hello-mod) and read [docs/MOD_API.md](docs/MOD_API.md). To find classes and methods to hook:

```bash
python tools/il2cpp_meta.py "<game>/The Walking Dead Destinies_Data/il2cpp_data/Metadata/global-metadata.dat" --grep Sensitivity
```

## Reporting bugs

Use the [issue forms](https://github.com/valerisn/Project-Lazarus/issues/new/choose). There's one for Lazarus bugs and one for bugs in the game itself (things we might be able to fix). Attach `lazarus.log`. For game bugs, say where in the game it happens; with the dev console, `load <level>` can often reproduce it without a save file.

Security problems go through [SECURITY.md](SECURITY.md), not public issues.

## Documentation

| doc | contents |
|---|---|
| [INSTALL](docs/INSTALL.md) | install, configure, uninstall, recovery, troubleshooting |
| [CONSOLE](docs/CONSOLE.md) | dev console commands and the level list |
| [MOD_API](docs/MOD_API.md) | writing mods against API v2 |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | boot sequence, lifecycle, failure policy |
| [FEASIBILITY](docs/FEASIBILITY.md) | build facts and why the loader is designed this way |
| [RESEARCH](docs/RESEARCH.md) | community-reported issues and evidence |
| [TESTING](docs/TESTING.md) | test suites and live test results |
| [CHANGELOG](CHANGELOG.md) | what changed |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md). The most useful contribution right now is running the [live verification steps](docs/TESTING.md#live-verification-procedure) and posting your log.

## License and legal

Project Lazarus code is MIT licensed. MinHook is BSD-2-Clause ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). This repository contains no game files, binaries, metadata or assets; you need your own copy of the game. Lazarus doesn't touch Steam, DRM or ownership checks.

Project Lazarus is an unofficial fan project. It isn't affiliated with or endorsed by GameMill Entertainment, Flux Games, AMC or Skybound. *The Walking Dead* and related marks belong to their respective owners.
