# Menu_Main.lua map (Milestone A research)

## Status
SUPERSEDED → WORKING implementation + full idiom map:
`docs/runtime/in-game-mod-menu.md` (Mods button + list screen live in-game,
2026-10-01). Key corrections vs the notes below: Clone_Find needs
`widget.agent` + THROWS on bad targets (not nil); rows must be added inside
`menu.Populate` (Menu_Add targets the current menu); DoString swallows
click-callback errors.

## Known (all from real bytecode via tools/lua52_dis.py)
- `Menu_Add(menu, widget, ...)` = proto 39 (Menu.lua:745-748): if upvalue0 set,
  return upvalue0(); else tail-forward to `Menu_Insert` (proto 38, 723-741).
  Upvalue identities Unknown (likely override/test hook - see Unknown).
- `Menu_Insert` → `Menu_Create` + `menu:AddWidget(...)` (+ AgentHide calls).
- Call shape (proven, repeated verbatim): `Menu_Add(R5, R6, R7, R8)` where
  R5=_ENV['ListButton'] (widget prototype), then id/label/callback strings,
  e.g. `('savesFiles', 'label_saveFiles', 'Menu_Saves()')`.
- LABEL CHAIN (proven): labelkey → `Menu_Text` (proto 40, Menu.lua:754-758) →
  `DlgEvaluateToNode('ui_menu.dlog', key, 'text', false)` → `node:GetText()`,
  fallback `'NIL VALUE'`. Labels are DLOG NODES, not landb-direct, not literals.
- CALLBACK CHAIN (proven): callback string stored as agent property
  `'Button - Command'` (UI_ListButton.lua:16-43 via `AgentSetProperty`);
  engine executes it on press. `DoString` referenced in same widget.
- ORDER = call order (append). Screenshot order matches code order modulo
  platform conditionals interleaved between adds.
- NAVIGATION = ListButton widget machinery (Clone_Find ui_listButton_button,
  chores Select/Deselect/Press, tooltip). No custom input needed.
- Button table: play/store/accountlink/savesFiles/achievements/stats/
  settings/help/exit with label keys + `Xxx()` callback strings (from consts).
- Scene `ui_menuMain.scene` holds camera/background/title only - no buttons.

## Observed
- `ui_menu.dlog` (238 KB, binary Meta) + tiny `chapters_english.landb`
  (140 B, parses to 0 entries - stub, not the string table).
- ToolKit `LanguageDb`/`LanguageResLocal.Text` types exist for landb work.
- Disassembler now prints RK-resolved constants (repo tool upgrade, tested).

## Hypothesis
- Missing label renders `'NIL VALUE'` (fallback path traced, untested live).
- landb write path + dlog node creation follow ToolKit dialog/language types
  (types surveyed, read/write round-trip UNTESTED).

## Unknown
- `Menu_Add`/`Menu_Insert` upvalue identities (override semantics?).
- Click execution thread/context; missing-callback behavior live.
- Which landb archive holds English UI strings; TextID allocation for new text.
- landb/dlog WRITE round-trip on real files (read surveyed only).

## Evidence
Full disassemblies (`Menu.lua` 45 fns, `Menu_Main.lua` 23 fns,
`UI_ListButton.lua`), const-level call mapping, ToolKit source + probes.

## Confidence
High: Menu_Add/label/callback/order/navigation mechanisms. Medium: landb
write feasibility (types exist, untested). See §10 gate.

## Prototype: Mods button via mirrored Menu_Add (IMPLEMENTED, VM-proven)

### Edit (tools/apply_mods_button.py - committed, pure transformation)

### Edit (tools/apply_mods_button.py - committed, pure transformation)
- Target: main.protos[16].protos[1] (Menu_Main line 397, main button list).
- Append const `'mods'` (label/callback reuse existing consts).
- Insert before final RETURN (pos 330), scratch regs R2-R6 (dead there):
  `Menu_Add(ListButton, 'mods', 'label_help', 'Menu_Options()')`
- label_help = TEMPORARY (renders Help text; landb write deferred per spec).
- callback = 'Menu_Options()' string DIRECTLY (opens Settings = safe,
  reversible, Back-able proof; no new proto, no main-chunk edit).
- Jump/lineinfo/locvar fixing automatic; maxstack untouched (regs < 11).

### Verification (static, all green)
- Round-trip: parse→serialize unmodified = byte-identical.
- Re-encrypt unmodified = byte-identical to original entry.
- Edit: all jumps valid, const/proto indices in bounds, regs < maxstack.
- Committed script reproduces prototype byte-exactly from fresh decrypt.
- Rebuilt archive: 456 entries, Tta4/deflate, modified entry verified exact.

### Runtime status
- Override fires 2x/run for Menu_data; game opens rebuilt archive, exit 0
  (weak-positive: parses without aborting boot).
- VISUAL proof pending: needs menu render (long session). Short test runs
  exit pre-menu consistently; background loop hunting a long session.
- Menu_Add semantics: ESTABLISHED. Label semantics: ESTABLISHED (dlog path).
- Archive rebuild: ESTABLISHED (456/456 byte-identical round-trip with a
  documented 1-line ToolKit writer fix: name-stream BYTE length, not page
  count - external tool, not vendored).
- Menu_data opens via CreateFileW 2x/session: ESTABLISHED (long session).
- landb exact entry: NOT YET (fallback 'NIL VALUE' acceptable for prototype
  per spec §8 temporary-reuse clause).
- VM EXECUTION PROOF (stock Lua 5.2.4): converted chunk + stub _ENV harness
  (`tools/build_harness.py`, `tools/harness_runner.lua`) runs the REAL
  modified function cleanly (`pcall ok=true`): 13 Menu_Add calls including
  `Menu_Add(table, "mods", "label_help", "Menu_Options()")` - exact expected
  args. Requires Telltale-isms shimmed (`table.getn`).
- RK ENCODING BUG caught by the harness (would have shipped broken):
  LOADK Bx is a DIRECT const index; B/C 9-bit operands set 0x100 for consts.
  First prototype version passed 324/322 (nil labels); fixed + re-proven.
- Temporary choices (documented): label `label_help` (renders Help text),
  callback string `Menu_Options()` (opens Settings - safe, Back-able proof).
- Override fires 2x/run for Menu_data; game opens rebuilt archive, exit 0.
- VISUAL proof pending: needs menu render (long session; background loop
  running; screenshots broken in this env - xdotool navigation scripted for
  when a long session lands).
-> PROTOTYPE MAY PROCEED (§13): mirrored Menu_Add block + global callback +
   temporary label fallback; long-session proof required.

## Status
Mapped from decrypted + disassembled bytecode (`tools/lua52_dis.py`,
validated byte-exact against stock `luac 5.2.4`). No game files modified.

## Known
- Main menu buttons are built IN LUA (scene `ui_menuMain.scene` holds only
  camera/background/title agents - no buttons).
- Button construction idiom (repeated verbatim per button):
  `Menu_Add(Widget, 'ListButton', '<id>', '<labelkey>', '<Callback()>')`
  i.e. bytecode: `GETTABUP Menu_Add; GETTABUP <widget>; LOADK id, label, cb;
  CALL 4 5 1`.
- Button table (id → label key → callback string):
  play → label_play([]/Savefile/Demo variants) → `Menu_Main_Play()`
  store → label_store → `Menu_Main_Store()`
  accountlink → label_accountLinking → `Main_AccountLink()`
  savesFiles → label_saveFiles → `Menu_Saves()`
  achievements → label_achievements → `PlatformOpenAchievementUI()`
  stats → label_stats → `Menu_Main_Stats()`
  settings → label_settings → `Menu_Options()`
  help → label_help → `PlatformOpenHelpUI()`
  exit → `Menu_Main_Exit()`
- Labels are localization KEYS (landb), not literals. Callbacks are strings
  evaluated on click (`Menu_Main_Play()` etc. resolve as globals).
- Relevant functions: `Menu_Main_Start` (boot), lines 53-82/110-181/185-536
  (init/property setup), 397-511 (button assembly incl. platform
  conditionals), 538-652 (account/cloud/play flow).

## Earlier research notes (partially superseded by Prototype above; kept for
evidence - button table and function map remain accurate)

## Button table reference (still accurate)

## Unknown (remaining)
- landb binary write path (fallback covers prototype).
- Click execution thread/context; missing-callback live behavior.

## Evidence (original research)
`/tmp` disassembly (NOT in repo - game-derived): full instruction listing
with resolved jump targets; `luac -l` cross-check on stock fixtures.

## Confidence (original research)
High: representation, idiom, table. (Insertion mechanics since PROVEN by
implementation above; archive rebuild PROVEN; CreateFileW route PROVEN.)

## Next test (originally)
Disassemble `Menu.lua:Menu_Add` → confirm label/callback handling → pick
insertion function → prototype edit on a COPY → round-trip + long-session run.
(DONE except long-session visual proof - background loop running.)
