# docs/research/runtime/lua-pipeline.md - MCSM1 script pipeline (M6)

## Status
Pipeline mapped through boot; script modification proven at the encrypted-byte
level via M5; Lua VM internals (states, bindings) remain Unknown.

## Known
- Lua 5.2.3, statically linked into `MinecraftStoryMode.exe` (no Lua DLL in
  the loaded-module survey; no lua imports/exports; only version/env strings
  survive stripping: `$LuaVersion: Lua 5.2.3 ...`, `LUA_PATH_5_2`).
- All loose `.lua` (resdesc + `AdventurePass.lua`) are Telltale-encrypted
  bytecode (`\x1bLEo` header), NOT source. Stock `1B 4C 75 61` appears nowhere.
- Boot file-IO order (CreateFileW map, ~400 opens): 153 loose resdesc luas
  (`_resdesc_50_<Archive>.lua`, `\\?\` prefix) → ~197 `.ttarch2` opens →
  saves (`Documents\Telltale Games\Minecraft - Story Mode\prefs.prop`,
  `elfdl.prop`, `session_*.estore`, all OUTSIDE game root → resolver ignores).
- No non-resdesc loose scripts exist: gameplay scripts live INSIDE ttarch2
  (M5 limitation confirmed: archived content bypasses CreateFileW).
- Engine canonicalizes opened files (observed: mod replacement opened, then
  re-opened via its on-disk canonical path - likely GetFinalPathName flow).
  Overrides still hold: the original is never opened.
- Saves/prefs are outside game root by design; resolver scope already excludes
  them (validated by real traffic).

## Pipeline model (proven links marked *)
```text
boot → resdesc loose loads* → archive handle opens* (197 ttarch2) →
script bytes (loose resdesc* / archived?) → decrypt (\x1bLEo) →
Lua 5.2.3 VM (in-exe) → execute → native bindings (Unknown)
```

## Observed
- Valid resdesc substitution (German107 bytes in German108 slot): consumed,
  exit 0 (M5).
- Corrupt resdesc (200 zero bytes): opened twice, tolerated SILENTLY, exit 0,
  no wine errors. Error semantics for this path = ignore-and-continue.
- `TTMOD_MODLIST` survey: exe + system DLLs + fmod/dinput chain; no script DLL.
- `TTMOD_LATE_HOOK_S` experiment: MinHook install at +2s on an
  import-verified kernel32 function SUCCEEDS and fires → MinHook failures are
  init-timing (loader/unpack race), not Wine-fundamental. Reopens post-unpack
  detours for M6+ with proper timing.

## Hypothesis
- Resdesc lua format = version header (`50` = resource version?) + archive
  content map; TTG-Tools documents `.lua`/`.lenc` decryption externally (GPL,
  reference-only - not reimplemented).
- `LoadResource` RVA 0x1139F0 consumes these paths (fired with identical
  resdesc list when detoured once); detour removed (Wine hang), signature kept.

## Unknown
- Lua state lifecycle, script environments, native binding table.
- Whether archived scripts EVER touch CreateFileW (none seen in boot window).
- resdesc internal format beyond "encrypted blob".
- Script caching behavior (no cache files observed; §18 open: change disappears
  with mod removal per matrix reruns - no stale state seen).

## Evidence
ttmod.log maps (`CreateFileW#1..400`), module survey, late-hook success lines,
corrupt-mod run (exit 0), M5 matrix reruns (below).

## Confidence
High: version/location/order/substitution-tolerance. Low: VM internals.

## Next test (M6+/M7)
Post-unpack MinHook timing window for observational Lua hooks
(`onScriptLoaded` from resdesc names needs no polling - names already flow).

## telltale_hook offset verification (2026-09-17, read-only, exe untouched)
External reference HW12Dev/telltale_hook (no license - ideas/offsets only,
no code copied) publishes MCSM1 absolute RVAs. Verified against our exe
(md5 171ff4fe…, size 0xb9d9c0, imagebase 0x400000):
- lua_newstate @0x611C80 -> file bytes 8A 38 F7 FC = EXACT match of our
  independently observed runtime anchor ("lua_newstate anchor bytes").
- ScriptManager__LoadResource @0x1139F0 -> 0F 70 CA 56 = EXACT match of our
  observed LoadResource candidate bytes.
- lua_pcallk @0x60D3B0, lua_gettop @0x60B860, luaL_loadstring @0x60EBF0,
  lua_setglobal @0x60CDD0, CRC64 @0x24A620, TTArchive2__Activate @0x6003D0
  all land inside .text (RVA-mapped, no OOB).
Caveat: exe is packed (no standard prologues statically); anchor match is
the verification, not disassembly. Technique validated for future M6 Lua
bridge: MinHook lua_newstate (capture lua_State*) + hook
ScriptManager__LoadResource (per-script inject) + luaL_dostring/pcallk.

## Bridge PROVEN in-game (2026-09-17, wine-11.17, :99, fw-only + prototype)
Framework DLL with lua_bridge (late +2.5s MinHook lua_newstate, live-prologue
anchor 55 8B EC 83 - packed file bytes 8A 38 F7 FC are replaced by the real
prologue once the unpacker reaches .text, hence the retry at 2.5/5/8s):
  [INFO] lua: hook installed (late, anchor-verified)
  [INFO] lua: lua_newstate observed, live state captured
  [INFO] lua: bridge ready (executed ttmod_bridge_ok=true, stack balanced)
Game exit 0 after proof; prototype override fires 2x alongside the bridge
(coexistence run, originals md5 OK). Proof chunk sets ONE new global,
touches nothing else; raw lua_State* never leaves the framework.

## Menu_Mods entry path (2026-09-17, implemented, visual proof pending)
- Prototype edit v2 (tools/apply_mods_button.py): +3 consts
  ('mods','label_mods','Menu_Mods()'), same +6-insn mirrored Menu_Add block.
  VM proof: Menu_Add(table,"mods","label_mods","Menu_Mods()"), pcall ok.
- Bridge registers `function Menu_Mods()` on EVERY captured state
  (idempotent): bumps ttmod_mods_calls, sets ttmod_mods_pressed, guarded
  `if Menu_Options then Menu_Options() end`. Live log:
  "lua: Menu_Mods registered (marker diagnostics armed)".
- Label honesty: ui_menu.dlog contains ZERO label_* strings (CRC-hashed
  nodes); no existing Mods node exists, so 'label_mods' renders NIL VALUE
  until the dlog/landb write path lands. Key identity is forward-correct.
- Stock Lua 5.2 dynamic test (tests/test_menumods_chunk.py): exists, called
  twice (counter==2), returned, transition ran.
- Live regression: bridge proof + registration + 2x override, exit 0.
- VISUAL (button visible/navigable/clicked): BLOCKED on menu render (hunter
  continues on :99 with the v2 bits).

## Hunting label v3 (2026-09-17): visible label + Menu_Mods callback
Per hunting spec: label reverted to known-good 'label_help' (renders real
text), callback stays 'Menu_Mods()'. VM: Menu_Add(table,"mods",
"label_help","Menu_Mods()") exact args. Archive rebuilt + verified
(staged md5 928b1d1c…). Live: bridge + registration + 2x override,
exit 0, 16/16 green. Hunter resumed on :99 with v3 bits (cap 140).
Notable: attempt-0074 lived 309s (timeout) with a black 2560x1600 window
and 12 probes, but never created the Lua VM (no newstate observed) and
never opened menu streams - long-lived pre-menu, NOT a menu session.

## Pre-Lua boot diagnosis (2026-09-17, hunter paused at 98 attempts)
- Added loader/windows/stage.hpp: additive "[STAGE ms]" timeline lines
  (new lines only; core Logger format untouched, tests unaffected).
- First timestamped normal boot: init 41ms, first open 173ms, first
  override 1489ms, lua_newstate 2709ms. Lua VM exists by ~3s normally.
- Attempt-0074 reclassification: 309s, black window, override fired, but
  lua_newstate NEVER fired. Short runs PASS the Lua gate in ~3s; the long
  run stalled BEFORE ScriptManager/Lua init with the render window up.
  Its wine tail shows later-boot COM/service calls (wbemprox, avrt audio,
  gameux VerifyAccess) then silence - blocked/waiting, not crashed.
- Historical 18-min run: raw log NOT recovered (only doc counts survive:
  Menu streams 2x + saves). Class-C evidence is second-hand.
- Divergence: normal runs clear the Lua gate at ~2.7s; 0074 never did in
  309s. Candidate: engine loader thread blocked pre-ScriptManager (asset/
  service wait); render thread independent (black window up). Thread/CPU
  state unrecoverable post-hoc - needs live /proc sampling next time.

## Stuck-diagnosis correction (2026-09-17, external sampling only)
- CORRECTION: all prior "Menu_ms/txmesh opens = 0" claims were a LOGGING-CAP
  artifact (default g_maxw=0 hides CreateFileW paths). With
  TTMOD_FILELOG_N=500, EVERY normal 10s run opens Menu_ms + Menu_txmesh once
  each and Menu_data twice, plus ~500 opens (resdesc sweep, episode/shader
  archives, save probes), then exits 0. Boot routinely passes Lua (2.7s) +
  menu resources. Evidence: test-results/stuck_diag/run2/launch-*.
- Failure mode A (fast, 11/12 runs): wine ends with
  err:d3d:wined3d_swapchain_resize_buffers "still holding back buffer 0"
  immediately before clean exit 0. STRONGLY SUGGESTED (not proven):
  presentation init fails under Xvfb/Wine GL → game quits deliberately.
- Failure mode B (rare, attempt-0074): 309s black window, pre-Lua stall,
  ZERO swapchain calls - never reached presentation init at all. Separate
  loader-wait issue, cause unknown (thread state unrecoverable post-hoc).
- Targeted next experiment (hypothesis-driven, not yet run): probe Xvfb
  GLX/visual capabilities vs wined3d requirements; if software GL lacks
  what the swapchain resize needs, that explains mode A deterministically.

## Presentation diagnosis (2026-09-17, observation only, no env changes)
- Xvfb :99 caps (test-results/presentation-diag/xvfb/): Mesa llvmpipe
  LLVM 22, GL 4.6 compat, GLX 1.4, multisample+sRGB present. NO concrete
  GLX-level mismatch for a D3D9-era title.
- WineD3D run (WINEDEBUG +timestamp,+d3d_swapchain,+d3d): adapter llvmpipe,
  swapchain created fullscreen, GL context ok, back buffer acquired,
  TWO presents ~2.3s later, then restore-from-fullscreen + resize to
  800x450 + "still holding back buffer 0" + clean exit 0. RENDERING WORKS
  briefly; presents are rare across runs (1/3 runs, 2 frames).
- Controlled :0 desktop comparison: IDENTICAL (Lua ok, same swapchain
  error, exit 0). Xvfb exonerated; failure is not display-specific.
- 0074 re-bound: reached d3d9 device_init ("Ignoring display mode",
  "No card selector for vendor 0000") + format queries, but NEVER
  swapchain_init and never Lua. Stall sits between device_init and
  swapchain creation - with the window up. Cause vs symptom still open.
- "Still holding back buffer 0" timing: during post-present
  resize/teardown, AFTER frames presented (run 1). Fatal vs incidental
  unresolved - no screenshot of presented frames captured yet.

## WM experiment (:100 + openbox 3.6.1) - focus hypothesis WEAKENED
Single controlled run under a real WM: init 38ms, Lua 2948ms, Menu streams
once each, bridge ok, 0 presents, SAME swapchain resize error, exit 0,
~10s lifetime. Indistinguishable from :99-no-WM across every signal.
Foreground/WM negotiation is NOT the quit driver. Cleanup verified
(:100 + openbox gone, :99 intact). Evidence: test-results/wm-test/.

## Exit caller IDENTIFIED (2026-09-17, +relay, 504MB log preserved)
- tid 05c4 (game thread): GetModuleHandleW(L"mscoree.dll") -> NULL
  (ret=00b5029a), then ExitProcess(0) from the SAME game function
  (ret=00b502cc, exe+0x7502CC, inside .text). Preceded by worker-thread
  drain (RtlExitUserThread wave). Second ExitProcess is Wine's own
  loader-thread shutdown, secondary.
- So: shutdown path checks for LOADED mscoree (.NET); Wine never loads
  it (stub exists on disk but unloaded) -> game exits 0. Whether the
  check IS the quit decision or teardown hygiene after a prior trigger
  (swapchain resize failure 1ms earlier) is NOT separated by this data.
- winedbg path TAINTED this question separately: under the debugger the
  game page-faults at game+0xBB4189 (unpacked region) instead of exiting
  cleanly - debugger presence changes behavior; that crash is NOT the
  standalone exit path. ptrace is blocked in this env (EPERM).
- Proposed single next experiment (not run): WINEDLLOVERRIDES=mscoree=n
  to preload Wine's stub, making the check succeed - predicts either
  continued boot or a new CLR-init failure. Reversible, env-only.

## mscoree gate: strong negative (2026-09-17, no override tested)
- Static: 17 PE imports (kernel/user/gdi/d3d/fmod/comctl/shell/ole...),
  delay-loads ONLY steam_api.dll + Galaxy.dll. NO mscoree anywhere.
- Runtime (+loaddll full trace): ZERO mscoree mentions - never requested,
  never loaded, never failed. Case A: GetModuleHandleW is NULL because
  nothing loads it; WINEDLLOVERRIDES cannot help (it only selects among
  loads that something requests). Override hypothesis INVALID, not tested.
- Context: this copy is cracked (3DMGAME/NoDVD/CODEX/ALI213/EmptySteamDepot,
  FitGirl tree nearby); exe timestamp 2016-05-20 plausible. Whether the
  mscoree check is genuine engine code or crack-adjacent cannot be told
  without a clean reference binary - stated, not resolved.
- How legit Windows could have it loaded: UNKNOWN from our evidence
  (Steam/Galaxy injectors don't document mscoree; no import suggests it).
  Next experiment (proposed, not run): force Wine's builtin mscoree loaded
  (e.g. one-line diagnostic LoadLibrary in framework init, reversible) and
  observe whether the check passing continues boot or hits CLR-init
  failure. Evidence: test-results/mscoree/wine.txt.

## mscoree RESOLVED (2026-09-17): exit mechanism, not the trigger
- Preload experiment: LoadLibraryW -> 0x74840000 resident=1, yet the game
  exited at the same ~10s point (RESULT B).
- +relay comparison: WITH mscoree resident the game takes GetProcAddress
  (mscoree,"CorExitProcess") and calls it; mscoree then calls ExitProcess.
  WITHOUT it, the game calls ExitProcess directly (same function +0x32).
  The game uses CorExitProcess as its CLR-aware shutdown routine - the
  quit DECISION is upstream either way. mscoree presence changes the exit
  API, never the outcome. Lead closed as a cause.
- Diagnostic code REMOVED from framework.cpp (not merely gated); rebuilt,
  16/16 green, Win32 checks pass, game+stash DLLs refreshed to clean build.

## Shutdown trigger: orderly task-drain, no message (2026-09-17, +relay)
- Exit helper 0x7502b5/0x7502b7 (exe RVAs, from unpacked .text dump):
  pushes code -> CorExitProcess-or-ExitProcess. 4 call sites; 3 push 0xff,
  1 (0x7504f0) propagates its own arg -> consistent with observed exit 0.
- Caller context is CRT doexit-style (terminator-table walk at 0x750312,
  C++ container teardown with SEH states) -> normal program termination,
  not a crash path.
- Precise order (relay clock): worker drain 057.783 -> swapchain resize
  err 057.794 -> DestroyWindow(main, game code 0x4F4F78) 057.801 ->
  ExitProcess(0) via 0x7502CC at 058.628.
- NO PostQuitMessage / WM_QUIT / WM_CLOSE anywhere in 7M-line trace.
  Main thread's final phase = semaphore job-dispatch drain (engine task
  system shutting workers down gracefully). The quit DECISION precedes
  057.7 inside game logic and is not visible at Win32 level.
- mscoree/CorExitProcess CONFIRMED not the trigger (shutdown hygiene).

## Lua-side probe: boundary result (2026-09-17, temp sink, FULLY REMOVED)
- Static: full quit path mapped from 70 decrypted menu scripts -
  Menu_Main_Exit (Menu_Main.lua:733, proto 21) = Licensed? Upsell :
  UI_Confirm(quit popup -> 'EngineQuit()'); Menu_Upsell_Exit falls back
  to EngineQuit() with no menu. Only ONE EngineQuit caller in menu Lua.
- Live inventory (temp C sink + nil-safe presence chunk, 5 runs): sink
  proven working ('sink-ok'), but fresh states are BARE sandboxes -
  tostring/pcall/pairs/string/os/table/rawget/EngineQuit/Menu_Main ALL
  nil at newstate time (2 states observed, both bare). Base libs attach
  later; bridge executes at birth+0 and can never see menu-time state.
- Multi-timestamp probing (5/8/10/15s) is NOT safely implementable under
  the freeze (needs timer/cross-thread calls or hijacking callbacks).
  Useful-negative verdict: shutdown decision not observable from
  bridge-reachable Lua. Temp code deleted (0 traces), 16/16 green.
- Next candidate (proposed, not built): late-install LoadResource detour
  (telltale_hook model) fires per script-load on the loader thread with
  initialized states - the only known late-Lua seam. Needs freeze review.
