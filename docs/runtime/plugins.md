# M3 plugin system (PROVEN under Wine)

## ABI v1 (`core/include/ttmod/plugin_api.h`, C ABI)
`struct ttmod_host { api_version, profile_id, game, season, log() }`.
Plugin exports `int ttmod_plugin_init(const ttmod_host*)`, returns 0.
`host->log` is thread-safe, callable anytime.

## Layout (next to the framework DLL)
```text
plugins/
  hello-mcsm/
    manifest.json   { id, name, version, api:1, games:["minecraft-story-mode:s1"] }
    plugin.dll
```

## Loader (`loader/windows/plugins.cpp`)
discovers `plugins/*` → parses manifest (portable `ttmod::parse_manifest`,
unit-tested in `tests/test_manifest.cpp`) → rejects with reason on:
missing manifest, bad manifest, api mismatch, game mismatch, missing DLL,
missing export → `LoadLibraryA` → calls init → logs `initialized (rc=N)`.
A bad plugin never stops the game or other plugins. No unload in M3.

## Proven log (real game)
```text
plugins: discovered hello-mcsm
plugins: hello.mcsm validated
hello-mcsm: hello from minecraft-story-mode (profile mcsm1_pc_x86, api 1)
plugins: hello.mcsm initialized (rc=0)
```

## Deferred (M10)
Dependencies, versions constraints, conflicts, load order, safe unload.

## ABI v2–v4 (later milestones, all additive)
- v2: `subscribe(event_id, cb, ctx)` / `unsubscribe(token)` - file/resdesc/
  archive open events, dispatched synchronously on the game thread.
- v3: `get_state(ttmod_state*)` - lifecycle snapshot (episodes seen,
  counters, save dir).
- v4: `get_mod_count()` / `get_mod_info(i, out*)` - installed-mod listing
  (what the in-game Mods screen shows).

## ABI v5 - Lua execution (`queue_ui_chunk`, 2026-10-02)
The superpower for "all kinds of mods": run your own Lua inside the game.

```c
if (host->api_version >= 5 && host->queue_ui_chunk) {
    host->queue_ui_chunk("if ttmod_menu_log then "
                         "ttmod_menu_log('my mod: hello from Lua') end");
}
```

Contract (read before using):
- Chunks run on the GAME's script thread - at the next script-resource
  load, on that Lua state, via the bridge's balanced-stack path. The
  loader never touches Lua from its own threads; plugins never should
  either. This call only QUEUES (thread-safe, non-blocking).
- No return channel: results/errors are invisible to the plugin. Debug via
  `ttmod_menu_log(s)` - registered on every captured state, lands in
  ttmod.log as `menumods-lua: <s>`.
- Fixed queue (16): flooding drops chunks (-1 returned, logged).
- Engine menu idioms that are PROVEN safe live in
  `docs/runtime/in-game-mod-menu.md` (Menu_Create/Populate/Menu_Push,
  Clone_Find on widget.agent). Click-callback DoStrings swallow errors -
  wrap risky engine calls in pcall and log.
- Game state reached: whichever state is loading a script at drain time -
  menu states during menu, gameplay states during play. Chunks run once.

Demo: `examples/event-log/plugin.cpp` queues a chunk at init; the
`menumods-lua: event-log: Lua chunk ran on the script thread` line in
ttmod.log is the live proof.
