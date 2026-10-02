# M11 mod packages + discovery (PROVEN under Wine)

## Format
`.ttmod` = standard ZIP (open with any archive tool). Layout = unpacked mod:
```text
manifest.json          # required at root, package_format 1 (default)
files/...              # resource overrides (game-path -> payload)
plugins/<name>.dll     # optional native code (manifest "plugin" to use it)
scripts/ assets/ docs/ # allowed; .git/build/logs/cache/dotfiles excluded
```
Package creation is deterministic (sorted entries, fixed timestamp/level;
byte-identical given same TZ). No custom archive, no required installer.

## Manifest additions (M11, all optional)
- `"package_format": 1` (reject >1)
- `"plugin": "plugins/foo.dll"` (legacy default `plugin.dll` at root)
- `"arch": "x86"|"x64"|"any"` (MCSM1 loads x86 PEs only; others rejected)

## Validation (no extraction, no execution)
Integrity, safe paths (no traversal/absolute/drive/UNC), no symlinks or
special files, exactly one root manifest, case-fold dedupe, every declared
`files{}` target + plugin path present, manifest schema incl. new fields.
`ttmod package validate` / `ttmod package info` expose this offline.

## Discovery (`mods/`, automatic, no CLI)
`.ttmod` + unpacked dirs with `manifest.json`; junk ignored silently;
api/game/state gating; duplicate IDs keep the unpacked dir (dev override);
every decision logged (`[TTMod] Skipped: id: reason`). Deterministic order.

## Cache (`ttmod/cache/<id>/`, framework-owned)
Validated packages extract here when new/changed (size+mtime marker); stale
entries removed when the source vanishes (disabled counts as vanished and
re-caches on re-enable). Non-marker dirs never touched. Runtime consumes
cache dirs and unpacked dirs through ONE code path.

## State (`config/mods.json`, framework/user-owned)
`{"id": {"enabled": false}}`. Missing entry = manifest default (normally
enabled). Packages are never modified. Disabled mods: no overrides, no
plugins, no events. Kept simple on purpose; GUI-ready later.

## Layout (auto-created on first run)
```text
mods/ config/ logs/ ttmod/cache/
```
Logs live in `logs/ttmod.log` (plus `ttmod.exit.log` with a state summary).

## Native code policy
Manifest-declared plugins only (never stray DLLs); PE arch verified;
`WARNING … can execute arbitrary code` logged on every native load.
Packages are NOT sandboxed - stated in logs, CLI info, and here.

## Proven (§33 + matrix case 9)
drop-in .ttmod → cached → override consumed → disable → silent →
delete → gone + cache cleaned → unpacked identical → originals md5-OK.
