# Lua / script runtime (M6 - researched, exposed via runtime bridge)

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
