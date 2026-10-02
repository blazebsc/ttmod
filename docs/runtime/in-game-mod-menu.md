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
- `loader/windows/lua_bridge.cpp` - late (2.5 s, anchor-verified) MinHook
  detours on game `lua_newstate` + `ScriptManager::LoadResource`; installs
  the Menu_Add wrapper chunk when `Menu.lua` loads and registers the menu
  bridge on EVERY captured Lua state.
- `loader/windows/menu_bridge.hpp::kMenuAddWrapChunk` - wraps `Menu_Add`,
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
- `loader/windows/menumods_ui.lua` (embedded as menumods_ui.h) - the screens:
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

## Plugin Lua queue (v5 ABI, 2026-10-02)
`host->queue_ui_chunk(code)` (see plugins.md) executes plugin-authored
Lua on the game's script thread; drained in the same LoadResource hook
that offers the Menu_Add wrapper, via the one shared `bridge_run_chunk`
(balanced-stack + error-sink). Unit queue semantics: `tests/test_uiqueue.cpp`.

## Toolchain: reading any game script offline (REPRODUCIBLE)
- Clone: `git clone https://github.com/iMrShadow/TelltaleToolKit` (MIT;
  the old Telltale-Modding-Group org URL is gone). Data folder: `data/`.
- Extract+decrypt helper: `tools/build_menu_button.sh` documents the C#
  shape; the actual runner used: csproj referencing
  `src/TelltaleToolKit/TelltaleToolKit.csproj`, then:
  `ws.LoadArchive(path,"m",1000)` → `ctx.ExtractFile("<Name>.lua")` →
  skip 4-byte `LEn` magic → Blowfish(profile key, 7) Decipher → prepend
  `\x1bLua` → stock Lua 5.2 bytecode. Needs `TTK_DATA=<toolkit>/data` in env
  (ArgumentNullException path1 otherwise) and a built csproj (`dotnet build`
  before `dotnet run --no-build`).
- Disassemble: `python3 tools/lua52_dis.py <file>.dec.lua` (RK-resolved
  constants; validated byte-exact vs stock luac 5.2.4).

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
- Script crypto: loose `.lua` = `LEo` + Blowfish(key `Mcsm`) → ASCII;
  archived = `LEn` + Blowfish → stock 5.2 bytecode + `\x1bLua` header.
  Round-trip byte-exact. (Version dispute settled: scripts are 5.2.)
- Menu streams open twice per long session (boot + revisit) via CreateFileW;
  whole-archive M5 override of Menu_data mechanically viable.
