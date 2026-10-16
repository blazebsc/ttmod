# Making mods

Five ways, simplest first. Every way needs the framework installed once
([Install](install.md)) and a folder or `.zip` in the game's `mods/`.
After every launch, confirm in `logs/ttmod.log` — each way below tells
you the exact line to look for. General breakage help:
[Troubleshooting](troubleshooting.md).

All five share one manifest. Minimum that always works:

```json
{
    "id": "my.first.mod",
    "name": "My First Mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"]
}
```

`api` is the oldest framework you need (`1` unless you use newer
features; today newest is `6`). `id` is lowercase-with-dots, unique
across all your mods. If two installed mods share an id, the unpacked
folder wins and the rest are skipped (logged, game unaffected).

## Way 1: replace game files (no code)

Override loose game files with your own bytes. The framework swaps them
at file-open time; the game never knows.

```text
mymod/
  manifest.json
  files/archives/_resdesc_50_German108.lua
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

Left side = game path, right side = your payload (relative, no `..`).
Higher `priority` wins when two mods replace the same file (ties go to
the smallest id — deterministic, logged).

Confirm: `override: <game path> -> <your file>` in the log.
Watch out: only files the engine opens loosely can be replaced —
content served from an already-open archive handle bypasses this layer.
Details: `../runtime/resource-overrides.md`.

## Way 2: Lua script (recommended for logic)

Runs on the TTMod-owned Lua 5.4 VM at startup — never the game's Lua,
so you can't crash the engine from here. Per-mod environments: your
writes are invisible to other mods; `ttmod.*` is shared.

```text
mymod/
  manifest.json
  main.lua
```

```json
{
    "id": "my.lua.mod",
    "name": "My Lua Mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "runtimes": ["lua"],
    "entrypoints": {"lua": "main.lua"}
}
```

```lua
ttmod.log.info('my lua mod is alive')
```

Available today: `ttmod.log.info(msg)` (logs `scripts: <msg>`),
`ttmod.version.string()`, and `ttmod.mods.list()` — the last one needs
`"permissions": ["mods.read"]` in your manifest, else the call fails
with `mods.read not granted`. Sandbox: base/table/string/math/utf8
only — no `os`, `io`, `package`, `debug`, file loading. Memory capped
at 256MB per VM. Working example: `examples/hello-lua/`.

Confirm: `scripts: my lua mod is alive` and `scripts: 1 ran, 0 failed`.
Failures show on the mod's in-game details screen as a
`Blocked: <file:line: message>` row — that row is your debugger.
Watch out: there is no scene/dialogue API and no access to the game's
own Lua state from here — that's Way 4.
Details: `../runtime/lua.md`.

## Way 3: Luau script

Same deal as Way 2, plus gradual-typing annotations, `+=`,
`continue`, backtick string interpolation, and `table.freeze/clone`.
Some authors prefer it; the framework treats both identically
(same registry, same `ttmod.*`, same sandbox shape plus Luau's own
`getfenv`/`setfenv` denial).

```json
{
    "id": "my.luau.mod",
    "name": "My Luau Mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "runtimes": ["luau"],
    "entrypoints": {"luau": "main.luau"}
}
```

Working example: `examples/hello-luau/`.
Details: `../runtime/luau.md`.

## Way 4: game Lua, `game.lua` (advanced)

Runs **on the game's own Lua states** (engine + menu worlds, separate
globals each). This is the only script path that can touch game
behavior directly — and the only one that can break the game, so the
rules are strict:

- Entry runs once per world: write idempotent code, guard with a
  `_TTMOD_LOADED` global — double side effects are your bug.
- Never block, never yield, never touch Lua from another thread.
- No return channel: debug with `ttmod_menu_log(s)` on menu states
  (lands in the log as `menumods-lua:`); engine-state failures vanish
  silently, same as the engine's own tolerance.

```json
{
    "id": "my.game.mod",
    "name": "My Game Mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "runtimes": ["telltale-lua"],
    "entrypoints": {"telltale-lua": "game.lua"}
}
```

Working example: `examples/hello-telltale/`.
Details: `../runtime/lua.md` ("Telltale-lua mods").

## Way 5: native plugin, C DLL (advanced)

Full native code: file/event observation, UI chunks, and registering
new `ttmod.*` functions that Ways 2–3 can call. x86 only (MCSM1 is
32-bit; the loader rejects anything else). Your DLL exports:

```c
int ttmod_plugin_init(const ttmod_host* host);
```

Return 0. Check `host->api_version` before touching newer fields;
append-only ABI, v6 current (`queue_ui_chunk` for UI chunks,
`register_api` for script bindings). `host->log` works anytime, any
thread. Every load logs a WARNING that native code runs wild — that
is the trust model, stated openly.

```json
{
    "id": "my.native.mod",
    "name": "My Native Mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "plugin": "plugins/mymod.dll",
    "arch": "x86"
}
```

Confirm: `plugins: <id> validated` and `plugins: <id> initialized
(rc=0)`. Zero `plugins:` lines = the declared path is wrong; a
`plugins/` folder with no `"plugin"` key loads as resource-only,
silently.
Details: `../runtime/native-abi.md`.
Working example: `examples/menu-theme/`.

## Shared: one mod, several ways

A single mod may ship any combination (`plugin.dll` + `main.lua` +
`game.lua` + `files/`). One init pass runs each part through its own
runtime in plan order; runtimes never share objects — native code and
Lua meet only through events, the chunk queue, and registered
bindings.

## Shared: settings your users can change

Add a `"config"` schema and the Mods menu builds the settings screen
(bool/int/float/string/enum/color rows); values land in
`config/<your-id>.json`, missing entries fall back to your defaults.
Never write that file yourself — the framework owns it.

```json
{
    "id": "my.themed.mod",
    "name": "My Themed Mod",
    "version": "1.0.0",
    "api": 1,
    "games": ["minecraft-story-mode:s1"],
    "config": [
        {"key": "accent", "type": "color", "label": "Accent color", "default": "#E0A040"}
    ]
}
```

Types: `bool` `int` (`min`/`max`/`step`) `float` `string` `enum`
(`options`) `color` (`#RRGGBB` — the menu row opens a 16-swatch palette
instead of a text box). Enable/disable stays in framework-owned
`config/mods.json` — never write it yourself. Effective rule, one
query everywhere: manifest `enabled` is the default, `config/mods.json`
wins when present (logged as `Skipped: <id>: disabled`).

## Shared: dependencies, enable/disable, packaging

- `"depends": [{"id": "base.mod", "version": "2.0"}]`,
  `"conflicts": ["rival.mod"]` — unmet means skipped with a logged
  reason. Details: `../runtime/mod-deps.md`.
- Ship `"enabled": true`; users override in `config/mods.json`
  without touching your files. Test both states.
- Ship it: develop as an unpacked folder (no repackaging loop), then
  `ttmod package create mymod/ mymod.zip` for a deterministic,
  validated ZIP. Drop the folder or the `.zip` into `mods/` — same
  system. Details: `../runtime/mod-packages.md`.

## What doesn't exist (so you don't go looking)

Scene/character/dialogue APIs, archived-script overrides (the engine
reads those past the file layer), general Lua-value APIs beyond the
documented `ttmod.*`, MCSM Season 2 support (profile defined, not
implemented — see `docs/games/mcsm2.md`). Policy for what gets added and
why: `../runtime/api-policy.md`.
