# Lua / script runtime (M6 — researched, exposed via runtime bridge)

## What is proven
> 2026-10-01 update: the in-game Mods menu SHIPPED using this bridge —
> Menu_Add wrapper + Lua screens live in the game's menu state. All verified
> idioms, the four-round bug history, and the offline decrypt/disasm pipeline
> are consolidated in `docs/runtime/in-game-mod-menu.md`.

- Lua 5.2.3, statically linked in the exe. No Lua module, imports, or API
  symbols (stripped); version/env strings only.
- All loose scripts are encrypted bytecode (`\x1bLEo`); stock Lua signature
  absent. Authoring new scripts needs the decrypt/build path (TTG-Tools
  documents it externally, GPL — not reused in-core).
- Resdesc substitution works (valid bytes consumed; corrupt bytes silently
  tolerated). See `docs/research/runtime/lua-pipeline.md`.
- Safe seams: CreateFileW IAT hook (file-level observation + override),
  late MinHook installs (timing matters — init-time detours on game code
  hang Wine; +2s installs work).

## Mod-facing surface today
- Resource-only script replacement via M5 (`files: {"archives/x.lua": ...}`).
- No `register_script` / `run_script` / Lua-value API yet (M6 criteria do not
  require them; VM internals still Unknown).

## Thread context (observed)
- Framework init: dedicated init thread at process attach.
- Resource loads (incl. scripts): game loader thread(s); hook callbacks must
  stay reentrancy-guarded (TLS) and non-blocking — current implementation is.

## Limitations
- Archived scripts bypass CreateFileW (engine-internal reads).
- No script error surfacing beyond engine tolerance (corrupt resdesc ignored).
- No caching behavior characterized (no cache files observed).
- Script security model Unknown — treat mod scripts as fully trusted code.

## Next (M7+)
`onScriptLoaded`-style events derived from resdesc/archive open names
(event-driven, no polling); post-unpack observational detours using the
late-install timing result.
