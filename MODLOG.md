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

1. **Hover tint (VERDICT 2026-10-04: no Lua route; native detour only).**
   The unpacked dump's widget property table (rva 0x84C6D8..0x84CB60) revealed
   `Trigger Entered Callback` / `Trigger Exited Callback` - settable STRING
   properties the engine fires BY NAME on mouse enter/exit. Registering them
   to re-apply the accent on exit was tried and failed (see verdict below).
   *IMPORTANT correction:* the earlier "the exe has no hover concept" finding
   was an artifact of searching the PACKED file - the strings only exist in
   unpacked memory. `TTMOD_DUMP_MEM` + string analysis is the reliable route.
   *2026-10-04 verdict: the callback route FAILED in-game.* Across every
   session the callbacks fired zero times on UI widgets (3D scene-trigger
   properties, not UI) and the mechanism was removed from menumods_ui.lua
   rather than left to mislead. User screenshot confirms the symptom: an
   unhovered row (Mods, no selection arrow) stays white instead of
   returning to accent. The 324-name sweep found no selected/hover color
   property and no Lua event fires on unhover, so instant restore is
   impossible from Lua - only a native detour into the selection
   render/state-restore path could fix it.
   *2026-10-04 audit (theme-audit, user session): the engine WRITES into
   `Text Color` - 15 of 31 painted labels overwritten. Dominant value
   0.878 gray = deselect restores STOCK, not accent; also 1.0 white
   (select) and 0.4 gray (disabled rows - leave alone). A native
   SetProperty-level filter substituting accent for near-white/stock
   writes is viable; needs the internal setter address (Lua-binding hook
   would miss engine-internal writes). Next: TTMod_DUMP_MEM unpacked
   image -> static analysis for the setter.
   *2026-10-04 (later): dump produced RVA 0x3842A0 as the Lua
   AgentSetProperty binding - but shipped a Lua-level wrapper instead
   (menumods_ui.lua theme_wrap_asp): game UI scripts drive hover writes
   through the same Lua global, so near-gray-white Text Color writes
   become the accent there. No native hook needed IF theme-sub lines
   appear in-game; native setter hunt resumes if they don't.
   *2026-10-04 (later): theme-sub ZERO in-game despite audit overwrites,
   H2 dead (wrap installed at Menu.lua load). Shipped log-only native
   probe on the binding (lua_bridge.cpp hook_aspset, anchor 55 8B EC 6A):
   logs prop + caller RVA + tick ms (cap 400). Hover session discriminates
   H1 (engine-region callers / silence) vs H3 (binding hits on hover).
   *2026-10-04 (later): 270 hits, ALL caller 0x610787 (Lua VM) - and ZERO
   Text Color traffic outside menu builds. Hover writes bypass the binding
   too: H1 confirmed (fully internal). Static analysis then found the real
   mechanism: Lua binding RolloverEnableTextColor at RVA 0x73F730 writes
   Text Color through native setters directly. Shipped Lua wrapper
   (theme_wrap_roll + cache-bypassing repaint): hover/unhover repaints
   accent. Also fixed dead early-install (literal referenced a local).
   Pending user hover verification (theme-roll lines).
   *2026-10-04 (later): theme-roll ZERO in-game, audit still overwrites.
   Enumerated the full binding registry from the dump instead: hover
   family is RolloverEnableTextColor/TextBackgroundColor/RolloverMesh/
   ResetStatus/RolledOnOffCallback + TextSetColor (0x730690). Added
   TextSetColor wrapper (number- and table-arg rules) alongside the
   rollover one - one hover session now discriminates all Lua paths at
   once via theme-sub/theme-roll/theme-tc lines. If none fire, next is
   registering our own RolledOff callback (engine fires it natively).
   *2026-10-04 (later): all three Lua wrappers ZERO in-game, audit shows
   only the legitimate disabled-gray. The Rollover binding (0x73F730) may
   still be driven natively or via localized script refs - both invisible
   to wrappers AND to the asp probe. Added log-only native probe on it
   (hook_rol, anchor = SEH prologue). Hover session decides: rol: hits
   during hover = live path -> skip-hook fix; silence = deeper setter.
   *2026-10-04 (later): 60s retry never matched - the Rollover region
   never executes in this menu flow (dead binding here). Pivoted to the
   native setters the binding would have called: 0x568430/0x5684B0
   (thiscall, ret $0xc, args desc/color*/flag per call sites). Log-only
   thiscall hooks (scol/scolB: 4 floats + caller + tick) on the same
   retry table. Hover session identifies the white-writer; then
   substitute accent floats in place (no Lua ABI needed) or skip.
   *2026-10-05 (REA/Ghidra pass): render dispatch FUN_005746a0 (per-frame
   from GameEngine::Loop) draws highlight via 0x56F820(800, flag, ...),
   flag=1 on rollover rows. Shipped highlight-flatten hook (uifx):
   flag 1->0 when a valid accent is configured (cached at install, no
   per-frame I/O), cdecl 7-arg, anchor-gated, TTMOD_UIFX=0 kills it.
   Selection still shows via arrow; dialogue highlights flatten too
   while themed (documented tradeoff).
   *2026-10-05 (close-out): uifx REVERTED same day (flag=click-armed;
   all buttons died). scol-substitute moved to the TRUE setter 0x568430
   (scolB proved a getter). Green/blue verified pixel-perfect in
   screenshots, hover lightens + sticks per engine selection model
   (stock-identical, invisible in white-on-white). Orange renders
   reddish at rest (channel-order quirk, symmetric hues unaffected).
   Removed asp/scolB/rol/uifx probes; kept scol substitute + Lua guards.
   Mod renamed to Text Recolour. Full record: docs/runtime/menu-theme.md.
   *2026-10-04 (later): scol/scolB installed; 555 hits, values ONLY pure
   white or pure black (accent never passes these setters - paint uses a
   different one), scolB white hits span the whole session incl. +20s.
   Shipped exact-white->accent SUBSTITUTE in scolB (config-gated, call
   preserved, black/gray/tints pass). scol stays log-only with raised
   cap to reveal its late traffic. Tradeoff noted: dialogue whites
   would theme too while the mod is enabled.
   Full property list found (never enumerable at runtime): Text Background
   Color, Text Shadow Color, Text Image Color, Text Color, Trigger Enabled,
   Trigger Target Name, Trigger Entered/Exited Callback, mbGameSelectable,
   mSelectionOrder, etc.
   Ghidra MCP is set up (headless server + opencode.json) if deeper RE is
   ever needed; the callback route should close this without a native detour.
2. **Offline script decryption is unsolved.**
   The "Blowfish key `Mcsm`" recipe that is repeated in community write-ups
   **does not work** (verified: distinct inputs decrypt to the same head). We
   read engine behaviour at runtime instead. Extension artifacts ("unpack-tools",
   "texpack") use the real Lua-function bridge (5 Lua globals registered via
   `AgentGetProperties`-symboled thunks at known RVAs) + `ffi`, no engine
   decryption.

3. **Ghidra RE of render loop (IN PROGRESS - WIP).**
   `TTMOD_DUMP_MEM` stage → `ghidra-mcp` server over `launch.sh`. Got this far
   (xref map, property list): two tools new to Ghidra MCP v7: 
   - `tools.ghidra.list_functions({ query: "UI_ListButton" })` to find the
     script class that owns button-logic
   - `tools.ghidra.xref_to_addr({ address: "0082C944" })` → xrefs to "Text Color"
     (props table) to walk to the Lua function that renders labels
   - `tools.ghidra.xref_from_addr({ address: ... })` similar, then flag the color-function.
   The Ghidra MCP tool is flaky between sessions (restart dies; keep a full
   server log). Diagnostic: `curl http://127.0.0.1:8089/check_connection` —
   empty = server dead. The workflow is only useful for ongoing static-analysis
   services (powered-off).

4. **Hover highlight is engine-owned.**
   `Text Color` *does* overwrite stock for us. The hover-white is rendered
   internally (no Lua property). The external probe surface that lands =
   "canvas clone". Two remaining pieces of evidence would prove this before
   you spend: 
   - In-game, from an interactive session: a row that is NOT hovered should show
     the accent. If it does, the problem exists on HOVER only (engine overrides
     entry point). If it *always* whites them, it's stock rendering.
   - The Ghidra analysis (task 3) would prove which address reads Text Color
     and what decides to write white.

5. **Interval timerhouse does not exist in engine.** We do not have a
   frame-callback hook, only the Lua init-time hooks (Menu_Add/Chore play path). The "Blowfish key `Mcsm`" recipe The "Blowfish key `Mcsm`" recipe
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

## 2026-10-05 reorg (architecture review + standards)

- Theme color rule unified in `core/theme_color.hpp` (`parse_accent`,
  `should_substitute`) with `tests/test_themecolor.cpp` golden vectors;
  Lua suite asserts the same literals. Native hook calls core (ADR-004).
- Standards: `.clang-format` (tuned to tree idioms, not LLVM purity),
  `.editorconfig`, `tools/check_format.py` (changed-lines gate;
  grandfathered tree), CI `format` job.
- Knowledge: `CONTEXT.md` glossary + `docs/adr/` (001 hover-restore,
  002 uifx, 003 setter-not-getter, 004 theme-color-in-core).
- Lua chunk self-test (`menumods: ui self-test defs=9/9`, load-safe, no
  `type()` calls) instead of init-phase ceremony - the chunk needs no
  runtime init. Dual-state uiqueue test deferred (one occurrence).
- Anchors/retry stay in loader/windows: thread + Win32 types can't move to
  portable core. Init-phase functions declined for the same reason the
  self-test exists - declaration order, not file count, is the hazard.

## 2026-10-05 refactor Stage A (foundation)

- `core/theme_color.hpp` already landed; this stage: `TTMOD_CORE_SRCS`
  single list, `cmake/{helpers,warnings,sanitizers}.cmake`,
  `ttmod_add_unit_test` (short ctest names preserved), baseline warnings
  (no -Werror), ASan/UBSan options (Linux-only), x86 enforcement for the
  win32 runtime, `CMakePresets.json` (linux-debug/release/asan/ubsan,
  win32-mingw), `compile_commands.json` on.
- C standard kept at gnu11 deliberately: strict `-std=c11` breaks vendored
  miniz (`fseeko`); C++ stays strict. 21/21 green on debug+ASan+UBSan,
  win32 gate green. CI jobs (native/cross/sanitize/format) all run on
  presets now.

## 2026-10-05 refactor Stage B (security)

- Canonical `is_valid_mod_id` + `validate_mod_relative_path` (core),
  single implementation (package entry validation delegates to it).
- nlohmann/json 3.11.3 vendored (MIT): strict manifest/config parsing,
  duplicate-key rejection, trailing-garbage rejection, 1MB cap.
- `tests/test_security.cpp`: traversal/absolute/dup/overflow vectors.
- Atomic config writes (tmp+flush+rename), transactional cache
  (tmp dir+rename, hash+size+schema identity, no mtime).
- Package caps centralized: 512MB file, 4096 entries, 64MB/entry,
  256MB total, 1MB manifest. Lua->filesystem ID guard at write path.

## 2026-10-05 refactor Stage C (domain model)

- `Version`/`VersionConstraint` (simplified dotted-numeric scheme by
  decision, documented in header); `compare_versions` delegates.
  `moddeps` evaluates constraints instead of raw string compare.
- Manifest split into nested structs (Identity/Compatibility/
  Dependency/Override/Plugin/Presentation); ~50 call sites migrated,
  Lua-visible shapes unchanged. `arch` is now `Architecture` enum
  (absent = Any, garbage rejected); profile status is `ProfileStatus`.
- `RuntimeMode`/`LuaDomain` deferred to Stages E/H (unused enums would
  be speculative; recorded here instead).

## 2026-10-05 refactor Stage D (discovery + dependencies)

- `ModSource{path, kind}` + `scan_mod_sources()`: one walk shared by
  runtime discovery and the CLI (CLI keeps unfiltered display incl.
  invalid entries). `Discovery` gains `invalid[]` (visible) + source
  kinds; unparseable manifests no longer vanish.
- `modgraph`: explicit `DependencyGraph` (missing/version/conflict/
  duplicate/cycle + topo load order, deterministic). Replaces
  per-mod `check_requirements` (deleted with moddeps.*); loaders
  resolve once and skip from the result. Plugin init order unchanged
  (discovery id-sorted; graph order available for a later pass).
