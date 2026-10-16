# Lua / script runtimes (shipped: TTMod Lua VM, Luau VM, telltale game.lua)

## What is proven
> 2026-10-01 update: the in-game Mods menu SHIPPED using this bridge -
> Menu_Add wrapper + Lua screens live in the game's menu state. All verified
> idioms, the four-round bug history, and the offline decrypt/disasm pipeline
> are consolidated in `docs/runtime/in-game-mod-menu.md`.

- Lua 5.2.3, statically linked in the exe. No Lua module, imports, or API
  symbols (stripped); version/env strings only.
- All loose scripts are encrypted bytecode (`\x1bLEo`); stock Lua signature
  absent. Authoring new scripts needs the decrypt/build path (TTG-Tools
  documents it externally, GPL - not reused in-core).
- Resdesc substitution works (valid bytes consumed; corrupt bytes silently
  tolerated). See `docs/research/runtime/lua-pipeline.md`.
- Safe seams: CreateFileW IAT hook (file-level observation + override),
  late MinHook installs (timing matters - init-time detours on game code
  hang Wine; +2s installs work).

## Mod-facing surface today
- Resource-only script replacement via M5 (`files: {"archives/x.lua": ...}`).
- ABI v5 `queue_ui_chunk`: plugins queue a Lua source chunk that runs on the
  game's script thread at the next script-resource load (results/errors are
  invisible to the plugin; queue holds 16). Chunks can call
  `ttmod_menu_log(...)` to trace into `ttmod.log`. No general
  `register_script` / Lua-value API beyond this.

## Thread context (observed)
- Framework init: dedicated init thread at process attach.
- Resource loads (incl. scripts): game loader thread(s); hook callbacks must
  stay reentrancy-guarded (TLS) and non-blocking - current implementation is.

## Limitations
- Archived scripts bypass CreateFileW (engine-internal reads).
- No script error surfacing beyond engine tolerance (corrupt resdesc ignored).
- No caching behavior characterized (no cache files observed).
- Script security model Unknown - treat mod scripts as fully trusted code.

## Next
General `onScriptLoaded`-style events beyond resdesc/archive opens, and
richer Lua-value APIs on top of the v5 chunk queue. M7 file/resdesc/archive
open events already ship (`events.cpp`, ABI v2); the late-install detour
timing result below is what the shipped bridge uses.

## Telltale-lua mods (PR 32, advanced)

A mod whose manifest declares the `telltale-lua` runtime with an
`entrypoints.telltale_lua` game.lua runs ON the game's own Lua states -
not the TTMod VM. At init, the loader queues each surviving mod's source
(ModPlan order; safe-mode/disabled/blocked excluded by construction)
through the v5 chunk queue: it executes on the game script thread at the
next script load, plus the menu-state replay. Consequences, stated
plainly:

- The entrypoint runs once per Lua world (engine drain + menu replay),
  with separate globals each time. Write idempotent entrypoints
  (guard with a `_TTMOD_LOADED` global) - double side effects are the
  author's bug, not the loader's.
- No return channel: results/errors are invisible to the framework.
  Debug via `ttmod_menu_log(s)` on menu states; engine-state failures
  vanish into engine tolerance (same as corrupt resdesc).
- Game-thread rules apply: never block, never yield, never touch Lua
  from another thread. No raw `lua_State*` is exposed to normal mods -
  there is nothing to hold.
- Missing/unreadable entrypoint or a full queue is a Bridge diag event
  (`entrypoint-unreadable`, `entrypoint-dropped`), not a crash.
- Treat mod game.lua as fully trusted code (same as the engine's own
  scripts); there is no sandbox on this path by design.

## TTMod Lua mods (PR 30 wiring — the recommended scripting path)

A mod whose manifest declares the `lua` runtime with an
`entrypoints.lua` main.lua runs on the **TTMod-owned Lua 5.4 VM** —
never the game's states. At framework init (after plugins, before the
menu), the loader replaces the Null placeholder with a real VM and runs
every enabled Lua mod's entrypoint in its own environment (PR 17):
writes are invisible to other mods, `ttmod.*` is shared.

- Entry API today: `ttmod.log.info(msg)` → `logs/ttmod.log` as
  `scripts: <msg>`; `ttmod.version.string()` → framework version;
  `ttmod.mods.list()` → installed-mod rows `{id, version, enabled,
  has_plugin, packaged}` - **gated**: the calling mod's manifest must
  declare `"permissions": ["mods.read"]`, else the call fails with
  `mods.read not granted` (PR 17 per-env capability filtering; outside
  an entrypoint run, framework code is trusted). Memory is capped at
  256MB per VM (approximate check at budget cadence); instruction budget
  is the backend default.
- Script failures surface on the mod's in-game details screen as a
  `Blocked: <file:line: message>` row and in the log
  (`scripts: <id> failed: ...`); success logs `scripts: N ran, M failed`.
- Safe mode: no VM is created, every Lua mod reports a Backend failure
  (truthful, no silent skips).
- Plugin-registered bindings (ABI v6) are installed before entrypoints
  run, on BOTH script VMs: `ttmod.native["<plugin-id>"].<fn>` is
  callable from Lua and Luau mods alike.
- Luau is wired too (since the PR 31 link-in): Luau mods run on the
  TTMod-owned Luau VM, same registry, same bindings, same envs. See
  `examples/hello-luau`.
- See `examples/hello-lua` for the minimal working mod.

**CONFIRMED IN-GAME (user session, 2026-10-15, release 0.13.0):** the
full chain logged on one launch - `script VM live`, `scripts: hello
from TTMod Lua`, `scripts: 1 ran, 0 failed`, hello-lua listed in the
in-game Mods menu, hello-telltale running on the game's Lua states
(once per state - 5 worlds that session, per the documented
per-world semantics), and PR 24 diag events for the native plugin.
Script mods are live, not claimed.

## Mixed-runtime mods (PR 33)

One mod may ship all three runtimes (`plugin.dll` + `main.lua` +
`game.lua`); one init pass runs each part through its own runtime in
ModPlan order: native init, game.lua queue, TTMod-Lua run (shared VM,
per-mod env). Runtimes never share objects - native code and Lua meet
only through events, the chunk queue, marshaled values, and
`GameObjectHandle`s. TTMod-Lua failures record `ModRunStatus`s that
surface as menu warnings (`Blocked:` rows). Native script-API
registration shipped with ABI v6 (PR 34). Per-env capability filtering
gates `ttmod.mods.list` on the calling mod's manifest today
(`mods.read`); extending it to native-registered bindings is future work.
