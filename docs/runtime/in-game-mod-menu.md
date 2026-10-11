# In-game Mods menu - WORKING (native UI strategy)

## Status (2026-10-01)
FULLY WORKING IN-GAME, screenshot-verified on the user's real install:
main menu has a Mods row (currently texted "Help" - see label limitation);
clicking it opens a native list of all installed mods (name, version,
[ON]/[OFF]); each mod opens a details screen (version, Enabled toggle,
per-type config rows incl. native text edit box, restart hint, Back);
toggles/settings persist to `config/mods.json` + `config/<id>.json` and
apply on restart. 20/20 tests green (15 native + 5 Python); win32 DLLs staged.

## Architecture (what actually ships)
- `loader/windows/lua/lua_bridge.cpp` - late (2.5 s, anchor-verified) MinHook
  detours on game `lua_newstate` + `ScriptManager::LoadResource`; installs
  the Menu_Add wrapper chunk when `Menu.lua` loads and registers the menu
  bridge on EVERY captured Lua state.
- `loader/windows/menu/menu_bridge.hpp::kMenuAddWrapChunk` - wraps `Menu_Add`,
  census-logs main-menu rows via AppendLog, re-arms on the 'play' row
  (always first → revisit re-append), and appends ONE Mods row when the
  'exit' row passes: `Menu_Add(ListButton,'mods','label_help',
  'if Menu_Mods then Menu_Mods() end')`, then overwrites the label to
  "Mods" via the game's own pattern (`Clone_Find(widget.agent,'label')` +
  `AgentSetProperty(lab,'Text String','Mods')`). EVERY engine call in the
  append is pcall'd - the unguarded widget-table Clone_Find KILLED THE
  WHOLE MAIN MENU live (bug history below).
- `loader/windows/menu_bridge.{hpp,cpp}` - 4 same-thread C functions:
  `ttmod_menu_refresh()` (fresh registry snapshot → `ttmod_menu` literal),
  `ttmod_menu_set_enabled(id,"1"/"0")` → `config/mods.json`,
  `ttmod_menu_set_value(id,key,v)` → validate + `config/<id>.json`,
  `ttmod_menu_log(s)` → `menumods-lua:` lines in ttmod.log.
- `loader/windows/menu/menumods_ui.lua` (embedded as menumods_ui.h) - the screens:
  list → details → toggle/config-edit → back. Defined on every state
  (whichever state the menu runs on gets them).

## Verified game-UI idioms (from disassembled game bytecode - do not rediscover)
Source: `MCSM_pc_Menu_data.ttarch2` → Menu_Main.lua, Menu_Options.lua,
UI_ListButton.lua (decrypt+disasm pipeline below).
- Sub-menu open (Menu_Options.lua fn at line 109): 
  `local menu = Menu_Create(ListMenu, 'ui_menu_options')` - 2 args, the
  string is the MENU NAME not a scene - then `menu.align = 'left'`
  ('center' only when `AgentExists('ui_menuPause.scene')`),
  `menu.background = {}`, `menu.Populate = function(self) <rows> end`,
  `Menu_Push(menu)`. ROWS MUST BE ADDED INSIDE Populate: `Menu_Add`
  targets the CURRENT menu; adding before push lands rows on the old menu
  and the pushed screen renders empty (the blank-screen bug, fixed).
- REVEAL-REPOPULATE (log-proven 2026-10-11): a pop that exposes a screen
  RE-RUNS that screen's `menu.Populate` (the engine rebuilds the
  revealed menu's rows). So: anything Populate must be true on every run
  (idempotent rows, fresh reads), and revealed screens rebuild
  themselves with whatever `Populate` reads.
- MENU_POP YIELDS ON LOCK + MENU_REPLACE (2026-10-11, cost a second
  user round-trip): `Menu_Pop` opens with
  `while Menu_StackMemberLocked() do Yield() end`; the lock is an OR of
  four flags including the transition flag `Menu_Show` holds and the
  click's own press state. `'Menu_Pop();<builder>()'` in a button
  command therefore suspends mid-command and completes LATE, its pop
  interleaving with the push across frames. NEVER pop+push in one
  click command. The game's own idiom for sibling screens is
  `Menu_Replace(menu, existing)` - with existing == currentMenu it is a
  pure in-place show, stack untouched, no pop at all (see menu-theme.md
  for the shipped picker flow).
- Main menu build (Menu_Main.lua): `Menu_Create(ListMenu,'ui_menuMain',
  'ui_menuMain.scene')` (3 args), `menu.align='left'`, then Menu_Show.
- Button labels (UI_ListButton.lua Initialize):
  `widget = Clone_Find(self.agent, 'ui_listButton_button')` and, when a
  label key is passed, `Clone_Find(widget.agent, 'label')` →
  `AgentSetProperty(lab, 'Text String', text)`. Menu_Add returns the widget
  TABLE - you MUST pass `.agent` to Clone_Find; passing the table makes
  Clone_Find THROW (kills the screen silently inside the click DoString).
  All label writes are pcall-probed in menumods_ui.lua.
- Click path: button property 'Button - Command' → `DoString(cb)` on
  press; DoString errors are SWALLOWED (nothing in logs, nothing on
  screen). Debug only via `ttmod_menu_log`.

## Bug history this session (why it took four rounds)
1. Append never fired: wrapper gated on `cb == 'Menu_Main_Exit()'`;
   live rows appeared to carry a different cb, so 0 appends across all
   runs. Fix: id-only trigger ('exit').
2. Whole menu dead (logo only, no input): the append's
   Clone_Find/AgentSetProperty literal overwrite errored INSIDE the
   wrapper and the error propagated into Menu_Main. Fix: removed all
   engine calls from the append.
3. Dead click: cb `'Menu_Mods()'` DoString'd on a state lacking Menu_Mods
   → nil call, silent. Fix: guarded cb `'if Menu_Mods then Menu_Mods() end'`.
4. Row vanished on menu revisit: one-shot guard never re-armed. Fix:
   `ttmod_appended = nil` on the 'play' row.
5. Blank screen: rows added pre-push (see idiom above). Fix: Populate.

## Remaining (cosmetic / future - do not claim done)
- Nothing blocking: button + screen labels both read "Mods" (header via the
  'ui_header_header' clone found in disassembled UI_Header.lua). The dlog
  `label_mods` route is no longer needed and stays unexplored.
- In-game install/remove: no Lua↔filesystem bridge for `mods/`; today it's
  drop/delete files + disable via config, exactly per README.

## Opt-out
`TTMOD_MENU=0` env or empty `config/menu-disabled` file skips the
Menu_Add wrapper offer (logged once); the start menu stays stock. Mod
loading and the plugin chunk queue are unaffected. Checked once per
process at the first Menu.lua load (`menumods_button_enabled()`).

## Runtime Lua quirks that COST a debug round (verified in-game)
- **`_G` is nil.** The stripped environment table is not exposed. Any
  `_G.Foo` reference throws `attempt to index global '_G' (a nil value)`.
  Use bare globals (`TTMOD_ACCENT = "..."`). This silently disabled menu
  theming for one full day: the code read `_G.TTMOD_ACCENT`, always nil, and
  returned early, so no colour was ever attempted.
- **`setmetatable` is nil too** (Lua 5.1, not 5.2). Also `table.unpack`.
- **`type` (and standard libs) may not exist at chunk-load time.** The UI chunk
  runs at `lua_newstate` capture, before the engine opens libs on that state.
  Top-level code must only DEFINE, never CALL: a load-time `type()` call
  killed the whole chunk in-game 2026-10-04 (`pcall=2 ... attempt to call
  global 'type'`), taking `Menu_Mods` and all painting with it. Lazy-install
  from `TTMOD_THEME_WIDGET` (runs during menu builds, libs present) instead.
- Stock `lua5_1` and `lua5_2` BOTH provide `_G` and `setmetatable`, so a suite
  run on either cannot catch either bug. `tests/lua/test_menumods_ui.py` now nils
  `_G` in its stubs and runs the proof on BOTH interpreters, and it fails if
  `_G` reappears in the UI.
- Click callbacks swallow errors: trace with `ttmod_menu_log(s)` →
  `menumods-lua:` lines in `logs/ttmod.log`.
- **`tostring()` on an engine agent can fault.** Agents are userdata whose
  `__tostring` is not safe; calling it inside a click/populate path killed the
  process mid-menu (2026-10-02, header label set, then silence + exit). Log
  property NAMES, never the agent. Compare read-back values only when
  `type()` says string/number.
- **Never spray unknown `AgentSetProperty` names during a screen build.**
  `paint()` is a deliberate no-op until `probe_props` has proven one property;
  discovery is a single explicit pass, not per-label retrying.
- `bridge_run_chunk` logs the pcall error MESSAGE, not just the code (a bare
  `pcall=2` names no cause, and these are the only diagnostics available).

## Label colour property: `Text Color` (PROVEN in-game 2026-10-03)
The in-game probe (below) settled it on the first clean run:
```
probe: Color         set=true reads=false      <- does not exist
probe: Tint Color    set=true reads=false      <- does not exist
probe: Font Color    set=true reads=false      <- does not exist
probe: Text Color    set=true reads=true type=table   <- THE ONE
probe-winner: Text Color (exists, value format differs)
```
- The property name is **`Text Color`** (exactly that spacing/casing).
- Its value is a **TABLE with NAMED fields, 0..1 FLOATS**. Read-back was
  `0.87843102216721,…,1`: writing integers 0..255 gets CLAMPED and renders
  stock - that clamping masqueraded as "the engine overwrote us" for a day.
  Write `{ r = ri/255, g = gi/255, b = bi/255, a = 1 }`; positional arrays and
  hex strings are silently ignored. `tests/lua/test_menumods_ui.py` pins the
  float form and the exact `#FF8000` value.
- **Hover/press is an engine-owned limitation (2026-10-03 verdict).** After the
  float fix the write provably holds (`verify-ok (exact)` on every label), yet
  hover still reverts: a 297-name read-only sweep found no hover/press
  property, and the exe has no hover concept at all (`MouseClick` only). The
  white row-highlight is the engine's own rendering from internal state.
  Fixing it needs a native detour into the widget drawing code - a new hook
  surface, forbidden by api-policy.md without a demanding mod.
- Only OUR Mods-menu labels are themed. Game screens are untouched.
- `paint()` stays a no-op until the probe has run once per session: the
  property is discovered, never guessed, on the first label clone built.

## Hover resets the colour (RESOLVED VERDICT: engine-owned, see above)
The 2026-10-02 hypothesis below ("the engine repaints each state from its own
property") turned out wrong - there IS no per-state property. The section is
kept for the method, which is still right: scan, never guess.

**Declaration order in menumods_ui.lua is load-bearing.** A reference to a later
`local` is a nil global at call time, and in a click callback that kills the
process instead of raising an error (cost a crash on 2026-10-02 - then twice
more, silently, in `theme_drain` and the retry queue). Declare above the use,
or forward-declare explicitly. `tests/lua/test_menumods_ui.py` runs the real file
and catches this class.

## Colour-property probe (diagnostic)
`TTMOD_PROBE=1` env (any value but `0`) or an empty `config/probe-props` file
turns on one-shot property enumeration: the first label clone the menu builds
is probed with ~28 candidate `AgentSetProperty` colour names, each result
logged as `probe: <name> set=<bool> reads=<bool> [type=… val=…]`, and the
winner as `probe-winner: <name>`. Off by default; costs one launch.
This is how UI property names are learned now - offline decryption of the
game's own UI Lua is unresolved (see toolchain section).

## Plugin chunks must run on the MENU state too (2026-10-03)
The game uses **two distinct `lua_State`s**: engine scripts (`_engine.lua`,
`StoryBoardTracker.lua`) and menu scripts (`Menu.lua`, and every state the
Mods menu is built in). The v5 queue drains inside `ScriptManager::LoadResource`,
which fires on the ENGINE state — so a global a plugin sets (`TTMOD_ACCENT`)
simply does not exist where `menumods_ui.lua` runs, and menu theming can never
work.

Fix: `menumods_register()` replays `uiqueue_recent()` (history, not the drained
queue) on the menu state, logging as `menu-plugin`. `uiqueue_take()` still
drains, so script-thread semantics are unchanged; the history is bounded (64)
and replayed per menu state. Unit semantics: `tests/unit/test_uiqueue.cpp`.

**General rule for mods:** a queued chunk that must affect the *menu* must not
assume it ran on the engine state.

## Plugin Lua queue (v5 ABI, 2026-10-02)
`host->queue_ui_chunk(code)` (see plugins.md) executes plugin-authored
Lua on the game's script thread; drained in the same LoadResource hook
that offers the Menu_Add wrapper, via the one shared `bridge_run_chunk`
(balanced-stack + error-sink). Unit queue semantics: `tests/unit/test_uiqueue.cpp`.

## Toolchain: reading game scripts offline (RESOLVED 2026-10-11)

> 2026-10-11: decryption WORKS. The 2026-10-02 failure was the KEY: the
> profile key `"Mcsm"` is the ttarch ARCHIVE key. The Lua chunk cipher
> (`\x1bLEn` files) uses a DIFFERENT, 55-byte binary key stored in the game
> image - that's why "Mcsm" produced random bytes. Full recipe below.

- Extraction (unchanged, works): clone
  `git clone https://github.com/iMrShadow/TelltaleToolKit` (MIT; the old
  Telltale-Modding-Group org URL is gone), build the library, then from a
  small csproj referencing `src/TelltaleToolKit/TelltaleToolKit.csproj`:
  `Toolkit.Initialize(); var archive = Toolkit.Instance
  .LoadArchive(<path>, "Mcsm");` then iterate `archive.GetAllEntries()`
  and `archive.OpenResource(entry.Name)`. No GameProfile registration
  needed when the key string is passed directly. `MCSM_pc_Menu_data.ttarch2`
  yields 70 `.lua` scripts (`Menu.lua`, `Menu_Stats.lua`,
  `WidgetInitializer.lua`, `UI_ListButton.lua`, ...);
  `MCSM_pc_Engine_Lua_data.ttarch2` yields 38 more (network/save infra).
- **The LEn chunk layer (the 2026-10-02 missing piece).** Extracted
  scripts start with a plaintext 4-byte magic `\x1bLEn` (compiled) or
  `\x1bLEo` (source); the body is Blowfish ECB over `(len-4) & ~7` bytes.
  Two non-obvious parts, both read straight out of the game image
  (`mcsm-unpacked.bin`, PE base 0x400000, .text file off = RVA-0xC00):
  - **Modified Blowfish**: `S[0][118]` is byte-reversed before the key
    schedule (the ToolKit's `Blowfish(key, 7)` does this for you; the
    engine's own `.rdata` S table at VA 0xc3da28 already ships it
    swapped). P at VA 0xc3ea28, S at 0xc3da28 - both otherwise standard
    Blowfish tables.
  - **The key**: a 55-byte NUL-terminated binary blob at VA 0xc3ea70
    (file offset 0x83d270), immediately after the P table. No ASCII key
    exists in the image. The engine's own init wrapper (VA 0x530ed0)
    pushes exactly this pointer into the key schedule (VA 0x5302a0);
    the chunk loader is at VA 0x513c8f..0x513d19 (singleton getter
    0x530e30, block-decrypt method 0x530950), and the in-place
    encrypt/decrypt pair for chunks sits at 0x511930/0x511980.
  - Recipe: `new Blowfish(key55, 7).Decipher(body, len)` → prepend
    `\x1bLua` → valid Lua bytecode. Decrypted chunks are 5.2 format,
    32-bit sizes (`52 00 01 04 04 04 04 00` + LUAC_TAIL) - the game
    profile JSON's `"luaVersion": "5.1.2"` is wrong.
- Disassembler (now applied for real): `python3 tools/lua52_dis.py
  <file>.dec.lua` reads them directly (RK-resolved constants; validated
  byte-exact vs stock luac 5.2.4).
- What the scripts answered (2026-10-11, colour-picker research): the
  game's own UI scripts set NO widget colour property anywhere (colours
  are authored scene data; the closest they get is `Text String`,
  `Button - Command`, `Button - Chore *`, `Group - Visible`), but they DO
  colour menu text dynamically via inline markup - see
  `menu-theme.md` for the `^color:#rrggbb^` idiom the picker now uses.
  Decrypted scripts are game-derived: keep them in /tmp, never in the
  repo or a release.
- Historical (2026-10-02 failure, kept because the diagnosis matters):
  the documented recipe with the `"Mcsm"` key produced random bytes;
  distinct-file decrypts showed identical heads (key/stream framing
  artifact). The profile key was simply the wrong key for this layer.
  The in-game `TTMOD_PROBE=1` property probe remains the faster route
  for learning live property names - offline decryption is for reading
  the game's own IDIOMS (how its scripts build menus, what they set).

## Historical (kept for evidence)
- Phase 1 (2026-09-17, wine-11.17): vanilla/framework/prototype all exited
  pre-menu in this headless env; prototype preserved; NOT a framework bug -
  real interactive sessions later proved everything.
- Hunter: 28 attempts / all early exits; env exits pre-menu ~99% headless -
  visual work needs the USER's interactive session, doctor's orders.

## Original research notes (superseded where marked, kept for evidence)
- Menu content lives in `MCSM_pc_Menu_{data,ms,txmesh,anichore}.ttarch2` +
  per-episode `MCSM_pc_PC_*_Menu_compressed.ttarch2`, described by loose
  `_resdesc_50_Menu.lua` / `_resdesc_50_PC_*_Menu_*.lua`.
- `MCSM_pc_Menu_data.ttarch2`: 456 entries incl. `Menu.lua`,
  `Menu_Main.lua`, `MenuBoot.lua`, all `Menu_*.lua` screens, widgets
  (`BouncyListBoxWidget`, `ButtonDispatch*`, `ClickText`, `CoverFlow`),
  `ui_menuMain.scene`, `chapters.dlog`, `chapters_*.landb`.
- Script crypto: loose `.lua` = `LEo` magic (4 bytes) then opaque bytes;
  archived = `LEn` magic then opaque bytes. **The 2026-09-17 claim that
  Blowfish(key `Mcsm`) yields ASCII / 5.2 bytecode is FALSIFIED 2026-10-02**
  (see the toolchain section: different inputs decrypt to the same head, so the
  key/framing is wrong). Format claims here are the only trustworthy part.
- Version dispute settled 2026-09-17: scripts are 5.2 (exe strings say
  5.2.3). That remains plausible; the in-game runtime, however, is missing
  5.2-only globals such as `setmetatable` (see the 5.1 note below).
- Menu streams open twice per long session (boot + revisit) via CreateFileW;
  whole-archive M5 override of Menu_data mechanically viable.
