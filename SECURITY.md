# Security Policy

## Reporting a vulnerability

**Please don't open a public issue for security problems.** Report them privately through GitHub instead:

[**Report a vulnerability**](https://github.com/valerisn/Project-Lazarus/security/advisories/new) (Security tab, then "Report a vulnerability")

Include what you found, how to reproduce it, which Lazarus version or commit you tested, and what an attacker could do with it. You should get a first response within 7 days. If it's confirmed, we'll work on a fix with you, credit you in the advisory and the [changelog](CHANGELOG.md) unless you'd rather stay anonymous, and publish the advisory once a fix is out.

## Supported versions

Project Lazarus is pre-release. Only the latest commit on `main` gets security fixes. Once there are tagged releases, the latest release will be supported too.

## How Lazarus thinks about security

Read this before deciding whether something is a vulnerability.

**Mods are native code.** A mod is a Windows DLL that runs inside the game process with your user account's permissions. It can do anything a program you run can do. Lazarus does **not** sandbox mods, and can't realistically do so. Enabling a mod means trusting its author, just like running an .exe.

What Lazarus *does* promise:

| promise | how |
|---|---|
| A mod runs only if you enabled it | Mods load only when their id is listed in `[mods] enabled` in `lazarus.ini`. Dropping a folder into `mods/` is never enough. |
| A manifest can't point at a DLL outside its folder | `mod.ini` `dll =` must be a bare file name; paths, drive letters and `..` are rejected. |
| A mod can't impersonate another | A mod's folder name must equal its id, and if two mods claim the same id, neither loads. Otherwise a decoy folder that sorts first could run its DLL under the id you enabled. |
| You can always get back to a clean game | SHIFT at startup (safe mode), `[loader] enabled = false`, automatic quarantine of mods that crash during startup, or deleting `version.dll`. |
| The installer only removes what it installed | Uninstall removes the files in its install manifest plus Lazarus's own runtime output, and leaves everything else (such as mods you added) in place. It won't delete a `version.dll` it didn't put there unless you pass `-Force`. A manifest containing paths outside `version.dll` and `Lazarus\` is refused outright. |
| No network activity | The loader and bundled mods make no network connections and never update themselves. |
| No elevated privileges | Nothing needs administrator rights. If the game folder isn't writable, the installer stops and says so instead of elevating. |
| Game files are never modified | Installing only adds `version.dll` and `Lazarus\`. |
| Logs are safe to share | Paths under your user profile are written as `%USERPROFILE%`, so a pasted log doesn't reveal your Windows username. |
| The console won't wipe your save by accident | Developer-cheat methods that delete or overwrite saves only run through the explicit `cheat-unsafe` command. |

## In scope

* Anything that makes Lazarus load or execute code you didn't enable (bypassing the enable list, manifest path checks, quarantine).
* The installer or uninstaller deleting, overwriting or moving files outside what its manifest records.
* The proxy `version.dll` behaving differently from the real Windows `version.dll` in a way that harms other programs, or doing anything in processes that aren't the game.
* DLL search-order problems introduced by Lazarus beyond the documented `version.dll` proxy.
* Config or manifest parsing bugs that crash the game or corrupt memory when given malformed files.
* Bugs in the bundled mods (`mods/`) with security impact.

## Out of scope

* What a mod you enabled does. That's the trust decision described above. A malicious third-party mod is something to report to its author, and to us if it misrepresents itself as an official Project Lazarus mod.
* Vulnerabilities in the game itself, Unity, or Steam. Report those to their vendors.
* Cheating, unlocking content or using the developer cheats in the dev console. This is a single-player game; those are features.
* Issues that require an attacker who can already write to your game folder. They could replace any DLL there without Lazarus.

## Distribution safety

Only download Lazarus from this repository's [releases](https://github.com/valerisn/Project-Lazarus/releases) or CI artifacts. Both are built by public CI from public source and ship `SHA256SUMS.txt`; the README explains how to check a file against it. Treat Lazarus builds from anywhere else as untrusted.

Build pipeline hardening: workflow actions are pinned to full commit SHAs and kept current by Dependabot. The default CI token is read-only, and only the release job gets write access. `main` can't be force-pushed or deleted. Secret scanning and push protection are on.

## Review history

* **2026-10-28, internal review.** Found and fixed: mod id spoofing via a decoy folder (medium), uninstall deleting user-added mods (medium), unvalidated install-manifest paths (low), save-deleting cheats reachable from the plain `cheat` command (low), Windows username in logs (low), unpinned CI actions with no published checksums (low), and graphics DLLs loaded by bare name (hardening). Commits c08e37f..9c184e1.
