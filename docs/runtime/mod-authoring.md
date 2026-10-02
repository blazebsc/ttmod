# TTMod Mod Authoring Guide (M18 - only implemented features)

## 1. Resource-only mod (simplest)
```text
mymod/
  manifest.json
  files/archives/_resdesc_50_German108.lua   # game path -> payload
```
```json
{
    "id": "mymod.german",
    "name": "My German resdesc mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "priority": 100,
    "files": {
        "archives/_resdesc_50_German108.lua": "files/archives/_resdesc_50_German108.lua"
    }
}
```
Game paths: root-relative (`archives/...`) or absolute. Payloads: relative
subpaths only (no `..`, `:`, leading `/`). Higher `priority` wins conflicts.

## 2. packaged mod
```sh
ttmod package create mymod/ mymod.ttmod   # deterministic ZIP
ttmod package validate mymod.ttmod
```
Drop the `.ttmod` (or the unpacked dir) into the game's `mods/`. Same system.

## 3. Native plugin mod
Add `"plugin": "plugins/my.dll"` (+ optional `"arch": "x86"`) and ship the
x86 DLL. It must export `int ttmod_plugin_init(const ttmod_host*)`.
Native code runs wild: users see a WARNING in the log on every load.

## 4. Events (ABI v2+)
```c
host->subscribe(TTMOD_EVENT_RESDESC_OPEN, my_cb, ctx);
// events: file(1) / resdesc(2) / archive(3); callback gets request paths,
// override info, success flag. Runs on the game thread: be quick, be safe.
```

## 5. Dependencies
```json
"depends": [{"id": "base.mod", "version": "2.0"}],
"conflicts": ["rival.mod"]
```
Missing/low-version/conflicting = your mod is skipped with a logged reason.

## 6. Enable/disable
Ship `"enabled": true` (default). Users override per-mod in
`config/mods.json` without touching your package. Test both states.

## 7. Mod menu + per-mod config
- The menu backend lists every installed mod via the host (id/version/enabled). No registration needed - ship a valid mod and it appears.
- Display + config convention: optional `"name"`, `"description"`, and `"config"` schema in your manifest:
```json
"config": [
    {"key": "level", "type": "int", "label": "Level",
     "default": 3, "min": 1, "max": 9, "step": 1},
    {"key": "fancy", "type": "bool", "label": "Fancy", "default": true},
    {"key": "mode", "type": "enum", "label": "Mode", "options": ["cozy", "wild"]}
]
```
  Types: `bool` `int` `float` `string` `enum`. Values live in
  `config/<your-id>.json` (same shape); missing/invalid entries fall back
  to schema defaults. The native Mods screen (list → details → config rows
  with Back + Restart-required hint) edits these through the framework -
  mods never touch files. Tapping a string row opens the engine's own
  modal text box (the save-rename idiom); cancel changes nothing.
- Enable/disable stays in framework-owned `config/mods.json` - never write it yourself.
- Install/remove/enable today = filesystem (`mods/` + `config/mods.json`);
  the in-game Mods screen also edits enable/config (see `in-game-mod-menu.md`).

## 8. What NOT to rely on
Scene/character/dialogue APIs (don't exist), archived-script overrides
(engine reads those past CreateFileW), general Lua-value APIs (only ABI v5
`queue_ui_chunk` for UI chunks exists), MCSM2 (detection only). See tier policy in `api-policy.md`.
