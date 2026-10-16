# Native plugin ABI (roadmap PR 20): formal contract

**Status:** IMPLEMENTED (v6 current). This page is the contract; `plugins.md` keeps
the build history and proven-log.

## Boundary

C ABI only (`core/include/ttmod/plugin_api.h`). No C++ standard-library
types cross it, in either direction. Win32 x86 only (the loader rejects
non-x86 PEs; `thread_id` is 32-bit LLP64 - noted in the header).

## Versioning

- `TTMOD_PLUGIN_API_VERSION` (now 6) revs on any field addition. History
  is append-only: v2 subscribe/unsubscribe, v3 get_state, v4 mod
  listing, v5 queue_ui_chunk, v6 register_api. Older plugins keep
  working (exact-match version checks in hello-mcsm/title-mcsm were
  replaced with range checks for exactly this reason).
- Rule: **append last, never insert or reorder** - every older offset is
  stable. Pinned by `tests/unit/test_plugin_abi.cpp` (relative field
  order; absolute offsets differ on LP64 test hosts so only order is
  asserted, plus exact fixed-buffer sizes).
- `kApiVersion` (`core/include/ttmod/version.hpp`) mirrors the header;
  the same test pins the mirror (AGENTS.md: the three move together).
- Negotiation, both directions:
  - Plugin side: check `host->api_version` before touching newer fields;
    `host->struct_size` bounds the struct (see the header comment).
    Examples use exact-match (`!= TTMOD_PLUGIN_API_VERSION` -> `-2`);
    plugins that only need older fields SHOULD use `>=` + the
    struct_size guard instead so they survive host revs.
  - Host side: always passes the newest struct it built; never assumes
    what the plugin was built against.

## Lifecycle

- States: Discovered -> Loaded (`LoadLibrary` ok) -> Initialized
  (`init` returned 0) / Failed (`init` returned nonzero). A missing
  export stays Loaded: kept loaded, never inited. Every state is
  terminal - nothing unloads, nothing retries, nothing re-inits.
- When init runs: one sequential pass over the ModPlan
  dependency-ordered set during framework init, one plugin at a time.
  The loader builds a host struct per plugin (stable addresses; plugins
  retain the pointer) differing only in `subscribe`, which records
  tokens into that plugin's record.
- Callbacks are valid from `subscribe()` until process end, EXCEPT a
  failed init: the loader unsubscribes that plugin's tokens
  synchronously right after the nonzero return, before the next plugin
  initializes (`failed (rc=N, revoked K callbacks)` in the log). A
  dispatch on the init thread cannot interleave; a cross-thread dispatch
  can, so revocation is prompt, not atomic. Check version FIRST in init
  (the event-log pattern), subscribe after.
- Nonzero rc otherwise changes nothing: the DLL stays loaded (no live
  unload path exists and none is planned - unloading with outstanding
  event callbacks is unsound), the failure is logged, the plugin is
  never retried. Disabling a mod at runtime requires a restart.
- `host->log` may be called anytime, including during init, from any
  thread (thread-safe).
- **DO NOT CLAIM DONE:** failed-init revocation is unit-tested but
  unexercised in-game (no shipped plugin fails init).

## Capability negotiation

- Manifests declare `runtimes` + `permissions` (parsed/validated in
  `manifest.hpp`; unknown names rejected - permissions are a boundary,
  not a hint).
- `ttmod::plugin_grants(profile, requested)` (`plugin_grant.hpp`) returns
  the subset the host ABI exposes a function for (GameRead/GameEvents/
  UI/ModsRead/Resources on v5). Unknown profile grants nothing.
- Enforcement, in `plugins_init`:
  - runtimes non-empty without Native + shipped DLL = contradictory ->
    refused with a reason. Empty runtimes = legacy = allowed.
  - grants are LOGGED per plugin (`grants: ...` / `(none requested)`).
- Honesty clause: LoadLibrary is arbitrary code and the loader says so
  (`WARNING ... can execute arbitrary code`) for every DLL. Grants never
  mean confinement - only "the host provides an API for this".

## Callbacks

- `subscribe(event_id, cb, ctx)` -> token > 0, or -1. `unsubscribe`
  best-effort. Events dispatch synchronously on the game (caller) thread;
  callbacks must be non-blocking and reentrant; event strings are valid
  during the callback only.
- Example (file/resdesc/archive open events — request paths, override
  info, success flag):
```c
host->subscribe(TTMOD_EVENT_RESDESC_OPEN, my_cb, ctx);
```

## Script execution (v5)

`queue_ui_chunk(source)` runs your Lua on the game's script thread —
the superpower for "all kinds of mods". Minimal use:

```c
if (host->api_version >= 5 && host->queue_ui_chunk) {
    host->queue_ui_chunk("if ttmod_menu_log then "
                         "ttmod_menu_log('my mod: hello from Lua') end");
}
```

Contract (read before using):
- This call only QUEUES (thread-safe, non-blocking). Chunks run at the
  next script-resource load, on that Lua state, via the bridge's
  balanced-stack path. The loader never touches Lua from its own
  threads; plugins never should either.
- No return channel: results/errors are invisible to the plugin. Debug
  via `ttmod_menu_log(s)` — registered on every captured state, lands
  in ttmod.log as `menumods-lua: <s>`.
- Fixed queue (16): flooding drops chunks (-1 returned, logged).
  Queued chunks also replay on the menu state (history holds 64).
- Engine menu idioms that are PROVEN safe live in
  `docs/runtime/in-game-mod-menu.md` (Menu_Create/Populate/Menu_Push,
  Clone_Find on widget.agent). Click-callback DoStrings swallow errors —
  wrap risky engine calls in pcall and log.
- Demo: `examples/event-log/plugin.cpp` queues a chunk at init; the
  `menumods-lua: event-log: Lua chunk ran on the script thread` line in
  ttmod.log is the live proof.

## Native script bindings (v6, PR 34)

`register_api(ns, name, cb, ctx, min_args, max_args)` exposes a plugin C
function to TTMod Lua as `ttmod.native["<plugin-id>"].<name>` (the
registry forces the namespace - the passed `ns` is informational; a
plugin cannot overwrite stable namespaces). Returns a slot token (>0),
or negative (bad key, duplicate, bad arity).

- Value crossing: nil/bool/number/string by value, everything else
  `TT_UNSUPPORTED`. Args borrow (valid during the call); ret strings are
  copied by the host during the call (stack buffers fine).
- Errors: nonzero return + `err` text surfaces as a Lua error; the VM
  stays usable. A throwing callback is caught host-side and reported
  structured (never a C++ exception across the boundary).
- Callback rules mirror events: non-blocking, reentrant, script thread.
  Registration is valid during init or later; attribution to the plugin
  record is init-scoped (same rule as subscribe).
- `min_args`/`max_args` are enforced by the registry before the callback
  runs (`-1` max = variadic). Installs on the live VM when present;
  the Null VM accepts and ignores (real activation with a real backend).
- hello-mcsm registers `ping` as the copy-paste demo (v6-gated with the
  `>=` + null-check idiom).

## Thread rules

- Game-thread-only: event callbacks already run there; `get_state` /
  mod listing are synchronous reads, safe anywhere but freshest on game
  thread. `queue_ui_chunk` only queues (thread-safe, never blocks on
  Lua); chunks run later on the script thread. Never touch Lua state
  from plugin threads - there is no API for it by design.

## String/memory ownership

- Host-owned strings (`profile_id`, `game`, event fields, mod-info out
  buffers): borrowed, valid as documented (event strings: callback
  duration; mod-info: the `out` struct the caller owns).
- Plugin-owned chunk source passed to `queue_ui_chunk` is copied by the
  host. `get_state` fills caller-owned memory.

## DO NOT CLAIM DONE

- Cross-profile grants (only mcsm1 exists; everything else grants
  nothing - correct but unexercised against a second profile).
- Failed-init revocation is unit-tested but unexercised in-game (no
  shipped plugin fails init).

## History (M3 proof log, frozen)

```text
plugins: discovered hello-mcsm
plugins: hello.mcsm validated
hello-mcsm: hello from minecraft-story-mode (profile mcsm1_pc_x86, api 1)
plugins: hello.mcsm initialized (rc=0)
```
