# Native plugin ABI (roadmap PR 20): formal contract

**Status:** IMPLEMENTED (v5). This page is the contract; `plugins.md` keeps
the build history and proven-log.

## Boundary

C ABI only (`core/include/ttmod/plugin_api.h`). No C++ standard-library
types cross it, in either direction. Win32 x86 only (the loader rejects
non-x86 PEs; `thread_id` is 32-bit LLP64 - noted in the header).

## Versioning

- `TTMOD_PLUGIN_API_VERSION` (now 5) revs on any field addition. History
  is append-only: v2 subscribe/unsubscribe, v3 get_state, v4 mod
  listing, v5 queue_ui_chunk. Older plugins keep working.
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

- Load: manifest validated -> arch/PE gates -> runtimes gate (below) ->
  `LoadLibraryA` -> `ttmod_plugin_init(&host)` -> rc logged.
- `init` returns 0 on success; nonzero is plugin-reported failure, logged
  (`initialized (rc=N)`), host takes no further action (isolation: a bad
  plugin never stops the game or other plugins).
- `host->log` may be called anytime, including during init, from any
  thread (thread-safe).
- Shutdown: there is none. No unload, by explicit policy (unloading live
  native code with outstanding event callbacks is unsound); a safe first
  release forbids it. Lifetime model is roadmap PR 21's scope.

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
- `shutdown`/unload semantics: PR 21, explicitly not this page.
- Native script-API registration: PR 34, explicitly not this page.
