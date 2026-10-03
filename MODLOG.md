# MODLOG — TTMod (Minecraft: Story Mode)

Working journal for the framework. Durable engine findings live in
[`docs/research/telltale-engine.md`](docs/research/telltale-engine.md) (which is
also a field note in the
[universal-modder](https://github.com/rehan-remade/universal-modder) knowledge
base — there was no Telltale entry, we are the first). This file holds the
**live state and next steps**, not a second copy of the findings.

## Where things are

| Thing | Path |
|---|---|
| Project repo | `ttmod/` (own git, pushed to github.com/blazebsc/ttmod) |
| Game install | `../Minecraft - Story Mode/` (never commit, never release) |
| Toolkit | `../universal-modder/` (own git); `um` on PATH |
| Offline work | `/tmp/opencode/` — extracted archives, TTK harness, decompiles |
| Mod logs | `../Minecraft - Story Mode/logs/ttmod.log` (**appends** across runs) |

Isolate the last run, always:

```sh
cd "Minecraft - Story Mode"
awk '/TTMod framework v/{buf=""} {buf=buf $0 "\n"} END{printf "%s",buf}' logs/ttmod.log
```

## Current state

- Version **0.12.0**, plugin ABI **v5**, 20/20 native tests, win32 gate green,
  **CI green** (both jobs) as of `d441c38`.
- Working in the real game: mod discovery, enable/disable, per-mod config,
  resource overrides, in-game Mods menu (list → details → config rows),
  config-driven label tinting.
- GitHub release `v0.12.0` exists as a **draft**; not published.

## Open, with the next concrete step

1. **Hover/press tint reverts to stock (engine-internal; NATIVE MILESTONE IN
   PROGRESS).** Property space is exhausted with proof (2026-10-03, ten
   instrumented launches): base `Text Color` is written as 0..1 floats and
   verified holding; `Selection Color` was found on the widget root and button
   clone via the dashed-namespace sweep and painted with the accent (root
   read-back confirms it), yet hover still reverts. 324 names x every agent in
   the real clone chain (`widget.agent -> ui_listButton_button -> .agent ->
   label`) found only `Text Color`, `Selection Color`, `Button - Command`.
   `AgentGetProperties` returns a 1-entry table (`__ScriptObject`) - useless
   for enumeration. The white is the engine's own highlight rendering.
   *Native route (authorized by the user, demand satisfied per api-policy.md):*
   - [x] `TTMOD_DUMP_MEM=<path>` tool (f5f0395): dumps the unpacked module
         image once, at Menu.lua load. One user launch produces the dump.
   - [ ] Ghidra over the dump: find the highlight/state-change function.
   - [ ] Detour late + anchor-verified (the lua_bridge pattern), expose as
         `TTMOD_EVENT_WIDGET_STATE` (ABI v6) so any mod can subscribe.
   - Do NOT attempt from Lua - there is nothing left to set.
2. **Offline script decryption is unsolved.** The "Blowfish key `Mcsm`" recipe The "Blowfish key `Mcsm`" recipe
   that is repeated in community write-ups **does not work** (verified: distinct
   inputs decrypt to the same head). We read engine behaviour at runtime instead.
3. **MCSM2** is detection-only, deliberately out of scope.

## Hard-won rules (full list in the field note)

- The runtime is **Lua 5.1 with `_G` nil**. `setmetatable` is nil too. Any
  `_G.X` or 5.2-only global is nil, and inside an engine callback that **kills
  the process** instead of raising a catchable error.
- Offline proofs must run on **both** lua5.1 and lua5.2 *and* nil out `_G` in the
  stubs; stock 5.2 alone hides every one of these bugs.
- Declaration order in the game Lua is load-bearing: a reference to a later
  `local` is a nil global at call time → process kill.
- Two Lua states (engine vs menu). Plugin globals set on the engine state do not
  exist on the menu state.
- A `plugins/` folder with no `"plugin"` manifest key loads silently as
  resource-only. Confirm `plugins: <id> initialized (rc=0)` in the log.
- Never `tostring()` an engine agent. Never spray unknown property names during
  a screen build.
- Headless Wine exits before the menu ~99% of the time; **visual verification
  needs the human's interactive session.**

## Verification rules here

- Native `ctest` 20/20 **and** the win32 cross-build both pass before staging.
- `tools/verify_win32.sh` is a hard gate: PE32/i386, undecorated
  `DirectInput8Create`, system-DLL imports only.
- `strings <dll> | grep <marker>` to confirm a change reached the DLL the game
  loads (a stale DLL cost a debug round once).
- `um publish check .` before any release.
