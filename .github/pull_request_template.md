## What this changes

<!-- one or two sentences. link the issue: "Fixes #12" -->

## How it was verified

<!-- be specific about which of these applies -->

- [ ] Unit / installer / e2e tests pass (`scripts/build.ps1`)
- [ ] New behavior has a test (unit for `src/core`, e2e scenario for loader/proxy behavior)
- [ ] Tested in the real game (build 12839205). Relevant `lazarus.log` lines below
- [ ] Not testable in game yet (say why, and mark the feature "experimental" in docs)

```text
paste relevant lazarus.log lines here
```

## Checklist

- [ ] No game files, metadata dumps or extracted assets added
- [ ] Docs updated (README status table, CHANGELOG, mod config comments) if behavior changed
- [ ] Mods that hook game code are pinned with `game_builds` in `mod.ini`
