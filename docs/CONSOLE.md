# Dev Console

Press **F6** in game to open the console. **Esc** or **F6** closes it. Up/Down scroll through history, and `clear` empties the window. Every line the console prints is also written to `Lazarus/logs/lazarus.log`, so a pasted log shows exactly what happened.

Enable it by adding `dev-console` to `[mods] enabled` in `Lazarus/lazarus.ini`. Settings live in `Lazarus/mods/dev-console/config.ini`.

> **Status:** built and tested against the fake engine (command routing, autoexec, log mirroring). The game-specific commands (`load`, the cheats, `find`) **have not been run in the real game yet.** See "What we found" for why they should work.

## What we found in the shipped build

The release build still contains the developers' own tooling:

* **Every level as a `SceneCollection` asset**, loaded through the game's own `LoadSceneManager`, so `collection.Load()` runs the normal loading screen and setup.
* **`Lucille.LucilleCheats`**: god mode, immortal, instakill, infinite ammo/stamina/adrenaline, invisible, skip objectives, end level, skip narrative/HQ, add skill points, unlock all characters, weapon swaps, defining-moment switches.
* **`PlaythroughAction`**, an enum with a `GoLevel…` entry for every mission, plus `LevelSelection.unity` and a `ScenesCollectionToCheats` list (the developers' level select).

It isn't known yet whether the release build actually spawns the `LucilleCheats` object. `cheats` reports that.

## Commands

| command | what it does |
|---|---|
| `help [filter]` | list commands |
| `mods` | loaded mods and their status |
| `collections [filter]` | level/scene collections currently in memory, with `#index` |
| `load <name part \| #index>` | load a level through the game's own loader, e.g. `load TooFarGone` |
| `scenes` | Unity scenes loaded right now |
| `cheats` | state of the developer cheats (or whether the cheat object exists) |
| `god`, `immortal`, `instakill`, `ammo`, `stamina`, `invisible` | toggle that cheat; prints the new state |
| `skipobj [n]` | skip the current objective(s) |
| `endlevel`, `skipnarrative`, `skiphq` | the developers' progression shortcuts |
| `cheat <Method> [int]` | call any `LucilleCheats` method, e.g. `cheat AddSkillPoints`. Methods that touch saves or delete things are refused. |
| `cheat-unsafe <Method> [int]` | same as `cheat`, but allows save-deleting/overwriting methods such as `CallOnDeleteMainSave`. Back up your save first. |
| `timescale [x]` | game speed (0 = frozen, 0.25 = slow motion, 4 = fast) |
| `fpscap [n]` | frame rate cap (-1 = unlimited) |
| `find <Ns.Class> [filter]` | names of live objects of a type |
| `methods <Ns.Class> [filter]`, `fields <Ns.Class>` | list a class's members |
| `call <Ns.Class> <Method>` | call a parameterless static method |
| `inspect <Ns.Class>[#n]` | every field of a live object, e.g. `inspect Lucille.BossSystem.BossSystem`. Not yet run in game |
| `invoke <Ns.Class>[#n] <Method> [args]` | call a method on a live object; args are `true`/`false`, numbers, text or `null`. Not yet run in game |
| `trace.list`, `trace.add`, `trace.mute` | from the method-trace mod: what's traced, add a method at runtime, pause logging |
| `echo`, `clear` | |

Other mods can add their own commands with `register_command` (API v2).

## Jumping straight to a level at startup

Put commands in `[autoexec]` in the console's `config.ini`. They run once per session, after the main menu loads:

```ini
[autoexec]
commands = load TooFarGone; god
after_scene = Main Menu
delay_frames = 120
```

## Level collections in this build

From the game's Addressables catalog. `load` matches any unique part of the name.

| # | collection | episode |
|---|---|---|
| 01 | HospitalMH | Days Gone Bye |
| 02 | RicksNeighborhood | Listen Up |
| 03 | AtlantaCity | Guts |
| 04 | GenericWoods01 | Life Worth Living |
| 05 | AtlantaCamp | Overrun (Vatos) |
| 06 | GenericWoods02 | What Lies Ahead |
| 07 | SchoolBloodletting | Bloodletting |
| 08 | HospitalMHTS19 | TS-19 / Haunted |
| 09 | GreeneFarmPrettyMuchDeadAlready | Pretty Much Dead Already |
| 10 | GreeneFarmBetterAngels | Better Angels |
| 11 | WGCFacilitySeed | Seed (prison) |
| 12 | WGCFacilityBasementSick | Sick |
| 13 / 13b | WGCFacilityKillerWithin / WGCFacilityAndrew | Killer Within / Andrew boss |
| 14 | GenericStore01_TakeCare | Take Care of Each Other |
| 15 | WoodBuryWalkWithMe | Walk With Me |
| 16 | WoodBuryMadeToSuffer | Made to Suffer |
| 17 | WoodBuryHounded | Hounded |
| 18 | GenericStore02_WhenTheDead | When the Dead Come Knocking |
| 19 / 19a | WoodBurySuicideKing / WoodBuryWalkerPit | Suicide King / walker pit |
| 20 / 20b | WGCFacilityInfected / InfectedNight | Infected |
| 21 | AtlantaSuburbsClear | Clear |
| 22 | GenericStore03ThisSorrowfulLife | This Sorrowful Life |
| 23 / 23a | WGCFacilityTooFarGone / TooFarGone_PreScene | **Too Far Gone (Governor tank finale)** |
| 24-28 | StandardStore01-05 | supply runs |
| HQ01-HQ14 | HQs | camps between missions |
| dev | Boss_Andrew, Boss_Rick_Shane, Boss_WalkerPit, Combat_Tests, LD_Tests, ... | developer test levels |

(Episode mapping is inferred from asset names. WGC Facility = West Georgia Correctional, the prison.)

Only collections already in memory can be loaded. If `collections` comes up short at the main menu, try again from inside a mission or HQ, and please post the log. Loading assets that aren't in memory yet is the next step if it turns out to be needed.

## Chasing a softlock (Governor finale, #4)

Enable `dev-console` and `method-trace` in `lazarus.ini` and set `log_level = debug`. method-trace ships with the finale's boss, QTE, defining-moment, objective and input-lock methods preloaded.

1. `load TooFarGone` (or `collections TooFarGone` to see the exact names), then `god`.
2. Play to the freeze or the "can only walk" phase.
3. Open the console and grab the state:
   * `inspect Lucille.BossSystem.BossSystem` (look at `_bossFightState`, `_bossPartState`, `_bossPart`, `_waitCutsceneEnd`, `_waitGameplayEnd`)
   * `inspect Lucille.ObjectiveSystem.ObjectiveController`
   * `inspect Lucille.DefiningMomentSystem.DefiningMomentController`
   * `inspect Lucille.BossSystem.BossDuelQTE`
   * `trace.list` (call counts show what's still ticking)
4. Things to try, one at a time (note which one, if any, unsticks it):
   * `invoke Lucille.BaseCharacter.PlayerInputsReader TogglePlayerInput true`
   * `invoke Lucille.BossSystem.BossDuelQTE OnQteSuccess`
   * `invoke Lucille.DefiningMomentSystem.DefiningMomentController TriggerResult true`
   * `invoke Lucille.BossSystem.BossSystem CutsceneEnd` / `GameplayEnd`
   * `skipobj`
5. Quit and attach `lazarus.log`. The trace lines right before the stall are the interesting part.

These calls poke the game's internal state. Back up your save first.
