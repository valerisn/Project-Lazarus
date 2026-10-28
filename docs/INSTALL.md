# Installation, Recovery and Troubleshooting

## Install

1. Download the release zip (or build it yourself, see the README). It contains `version.dll`, a `Lazarus` folder, `Install-Lazarus.ps1`, `Install.bat` and `Uninstall.bat`.
2. Close the game.
3. Run `Install.bat`. It finds the game, shows the folder it found, and asks before changing anything.
   * Auto-detection checks Steam's registry entry, every library listed in `steamapps/libraryfolders.vdf`, `appmanifest_2192790.acf`, and each library's `steamapps/common`. If none of those match, it searches your drives (secondary drives first).
   * To skip detection: `Install.bat "E:\path\to\The Walking Dead Destinies"`, or `Install-Lazarus.ps1 -GameDir "..."`.
4. Start the game normally through Steam.

The installer adds **only** `version.dll` and the `Lazarus\` folder. It never modifies an original game file. If a `version.dll` from another tool is already there, it's backed up to `Lazarus\backup\` and restored on uninstall. Administrator rights are not needed unless the game is installed somewhere your account can't write to.

The installer warns, and refuses unless you pass `-Force`, if the game's Unity version isn't 2021.3.17 or the build isn't IL2CPP. An unrecognized game build is only a warning. The loader still runs, but mods pinned to a specific build are skipped.

## Configure

* `Lazarus\lazarus.ini`: loader settings and the list of enabled mods. **A mod only runs if its id is listed under `[mods] enabled`.**
* `Lazarus\mods\graphics-settings\config.ini`: frame cap, vsync, window mode, resolution, quality level, LOD bias, anisotropic filtering. Blank means "leave the game's value alone".

Reinstalling or upgrading never overwrites these two kinds of files.

## Uninstall

Run `Uninstall.bat` (or `Install-Lazarus.ps1 -Uninstall`). It reads `Lazarus\install-manifest.txt` and:

* removes `version.dll`, but only if it's still the exact file Lazarus installed;
* restores any `version.dll` it backed up;
* removes every file the installer put in `Lazarus\`, plus what Lazarus writes at runtime (logs, crash state, backups, key-rebind's `actions-*.json`);
* removes folders that end up empty.

**Anything you added yourself, like a third-party mod in `Lazarus\mods\`, is left alone** and listed at the end, so you can decide what to do with it. Add `-KeepSettings` to also keep your `.ini` files.

If the install record contains paths outside `version.dll` and `Lazarus\` (meaning it was edited or damaged), uninstall refuses to touch anything and tells you to remove the files by hand.

Manual removal also works: delete `version.dll` and the `Lazarus` folder from the game folder. That's all Lazarus ever adds.

## Logs

| file | contents |
|---|---|
| `Lazarus\logs\lazarus.log` | everything from the current session |
| `Lazarus\logs\lazarus.prev.log` | the previous session, kept so a crash log survives the relaunch |
| `Lazarus\logs\bootstrap.log` | the first three steps of startup. If this exists but `lazarus.log` doesn't, the proxy loaded but never saw IL2CPP start. |

`Install-Lazarus.ps1 -Status` prints the install state and the end of the log. `-OpenLogs` opens the folder.

## Recovery

| problem | fix |
|---|---|
| game crashes or misbehaves after enabling a mod | hold **SHIFT** while the game starts (safe mode: loader runs, no mods load), then remove the mod from `enabled` |
| a mod crashed the game while starting | nothing to do. Next launch it's quarantined automatically and listed in `Lazarus\state\quarantine.txt`. Delete its line to try it again. |
| want Lazarus fully inactive without uninstalling | `[loader] enabled = false` in `lazarus.ini` |
| safe mode without a keyboard (e.g. Steam Deck) | set environment variable `LAZARUS_SAFE_MODE=1`, or `safe_mode = true` in `lazarus.ini` |
| anything else | delete `version.dll` from the game folder. The game is then exactly stock. |

## Troubleshooting

**Black screen after the GameMill/Flux logos.** This is a known problem with the game itself, not Lazarus. One player fixed it by moving the game out of a folder whose name ended in a space (`C:/Steam /`). Try with Lazarus uninstalled first to tell the two apart.

**Settings in graphics-settings don't apply.** Check `lazarus.log` for lines tagged `[mod:graphics-settings]`. It logs every value it sets, what the engine reports back, and any value the game later changed. "engine call ... not available" means the Unity internal call couldn't be resolved. Please report that with your log.

**"game build ... is not one Lazarus has been tested on"**: the game was patched. The loader still works; build-pinned mods are skipped. Please open an issue with the fingerprint from the log.

**No `Lazarus\logs` folder after launching**: the proxy didn't load. Check that `version.dll` is next to `The Walking Dead Destinies.exe` and not in a subfolder.
