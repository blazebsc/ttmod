# M7 runtime events (PROVEN under Wine)

## Bus (`core/events.*`, portable, unit-tested)
Synchronous `subscribe/unsubscribe/dispatch`. Dispatch runs subscribers inline
on the caller thread; callbacks must be non-blocking and reentrant-safe.
`dispatch` iterates a snapshot view; mutating the bus from a callback is
forbidden (documented).

## Events (precise names - file request + open ATTEMPT, not execution)
- `onFileOpen` (1): any CreateFileW through the hook.
- `onResdescOpen` (2): basename `_resdesc_*.lua` / `_rescdesc_*.lua`.
- `onArchiveOpen` (3): basename `*.ttarch2`.
- Classification is a pure function of the normalized path (tested).

Event data: requested, normalized, resolved, winner, category, overridden,
succeeded (real handle valid - factual, proven by a failed `openssl.cnf`
open with `ok=0`), caller thread id. Strings valid during callback only.

## Ordering (guaranteed by hook structure)
request → resolve → override? → real open → event. The event always
post-dates the open attempt; `succeeded` is therefore factual.

## Threading
Callbacks run on the game thread that opened the file (loader thread at boot
in all observations). The file-hook TLS guard is held across dispatch, so
plugin file IO inside callbacks cannot recurse. No polling anywhere in the
event path (the title-mcsm poll is a demo-plugin choice, not architecture).

## Plugin ABI v2 (additive, v1-compatible)
Host appends `subscribe/unsubscribe`; v1 plugins (first 5 fields) verified
working unchanged (`hello.mcsm initialized (rc=0)` next to v2 subscribers).
Loader accepts manifest api 1..5 (anything in `1..TTMOD_PLUGIN_API_VERSION`). `subscribe` returns token or -1.

## Lifetime rule (learned the hard way)
The `ttmod_host` passed to `ttmod_plugin_init` MUST be static/global: plugins
retain it for async callbacks. A stack instance dangles after `plugins_init`
returns - this silently broke all callbacks until fixed (init-time logging
worked by luck of intact stack).

## Demo (`examples/event-log`, api v2)
Subscribes to all three; logs first 3 per category + every 100th total.
Proven: `resdesc #99`, `archive #145`, `other #1 ok=0` in a normal boot.
