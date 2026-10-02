# M7 runtime events (PROVEN under Wine)

## Bus (`core/events.*`, portable, unit-tested)
Synchronous `subscribe/unsubscribe/dispatch`. Dispatch runs subscribers inline
on the caller thread; callbacks must be non-blocking and reentrant-safe.
`dispatch` snapshots subscriber callbacks under an internal lock, then invokes
the copy with the lock released, so callbacks may freely
subscribe/unsubscribe and call sites need no snapshot/lock dance. One edge:
unsubscribe-during-dispatch may still deliver one in-flight stale call
(copied before removal); harmless, strings are per-dispatch copies.

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

## State summary (struct-derived, never parsed from log text)
`events_state_summary()` snapshots the live `GameTracker` struct and formats
one line (`state: episodes=[...] archives=.. resdesc=.. saves=.. others=..
save_dir=..`) with `snprintf` only. `exit_dump()` writes that line to
`ttmod.exit.log` at detach, plus a structured JSON twin from
`events_snapshot_json()` (same snapshot, portable `format_snapshot_json()`
renderer — the primary machine artifact). Nothing greps `ttmod.log`/`CreateFileW#` for live
counts; `ttmod map` replays old logs for forensics only (extraction, then the
same struct feed). `menumods-lua:` lines are UI trace only, never a count
source. All rendered lines stay byte-identical so existing grep keeps working.
