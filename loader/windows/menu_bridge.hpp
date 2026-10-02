// Native Mods-menu data bridge (internal, framework-owned).
// Reads the authoritative C++ registry (discovery manifests + ModState +
// per-mod config files) fresh on every refresh, and exposes exactly four
// same-thread C functions to menu Lua:
//   ttmod_menu_refresh()            rebuild ttmod_menu table literal
//   ttmod_menu_set_enabled(id,"1")  persist enabled state (restart applies)
//   ttmod_menu_set_value(id,key,v)  validate + persist one config value
//   ttmod_menu_log(s)               one log line (Lua-side UI tracing; the
//                                   menu state swallows DoString errors, so
//                                   silent "dead button" is undebuggable
//                                   without this)
// No raw pointers cross into Lua; no filesystem access from Lua; no ABI
// change. All functions are reentrant-safe on the calling Lua thread.
#pragma once
#ifdef _WIN32
#include "lua_abi.hpp"
namespace ttmod_win {
void menumods_init(const char* game_dir, const char* log_path);
// v5 plugin API thunk: queue a Lua chunk for the bridge to run on the
// game's script thread at the next script load. 0 queued, -1 dropped.
int menumods_queue_ui_chunk(const char* code);
// False when the user opted out of the in-game menu: env TTMOD_MENU=0 or
// the file config/menu-disabled. Logged once per process.
bool menumods_button_enabled();
// Register the three C functions + define the UI chunk on state L.
// Function pointers are the verified Lua ABI (same table as the bridge).
void menumods_register(lua_State* L, LuaLoadstringFn loadstring, LuaPcallkFn pcallk,
                       LuaGettopFn gettop, LuaTolstringFn tolstring,
                       LuaPushCClosureFn pushcclosure, LuaSetglobalFn setglobal);
// Game-idiom menu Lua chunks (loader-owned; moved from core — core keeps
// only portable RVA/anchor validation). Single source of truth, pinned by
// tests/test_menuadd_wrap.py + tests/test_menumods_chunk.py.
inline constexpr const char* kMenuModsFn = "Menu_Mods";
inline constexpr const char* kMenuModsCalls = "ttmod_mods_calls";
inline constexpr const char* kMenuModsPressed = "ttmod_mods_pressed";
// Menu_Mods entry point (idempotent: safe to run on every captured state).
// Markers distinguish exists (type(Menu_Mods)=="function"), called
// (ttmod_mods_calls increments), returned (chunk pcall==0 in harness).
// The guarded Menu_Options transition reuses the proven-safe target.
inline constexpr const char* kMenuModsChunk =
    "function Menu_Mods() "
    "ttmod_mods_calls = (ttmod_mods_calls or 0) + 1 "
    "ttmod_mods_pressed = true "
    "if Menu_Options then Menu_Options() end "
    "end";
// Menu_Add wrapper: installed once when Menu.lua finishes loading (it
// defines Menu_Add). Census: reports every call whose id matches a known
// main-menu row (zero library calls — plain == only, safe in bare states).
// On the main menu's 'exit' row (id-only trigger: live rows carry a
// callback string that differs from the researched one, so gating on it
// silently never fired — append: 0 across all live runs), appends ONE Mods
// row (Menu_Main:397 pattern). One-shot per menu build: the 'play' row
// (always first) re-arms the guard, so menu revisits re-append — reported
// symptom: Mods row vanished after leaving and returning to the menu.
// The row's label is overwritten to "Mods" via the game's own literal-
// overwrite pattern — Clone_Find on the widget's .agent (the widget TABLE
// makes Clone_Find THROW; that live error once killed the whole menu, so
// every engine call in the append is pcall'd). Callback stays guarded
// ('if Menu_Mods then … end'): a click DoStrings on a state that may lack
// Menu_Mods, and an unguarded call there = silent dead click (reported).
// The actual exit cb is reported once via AppendLog for the record.
// No bytecode edits anywhere.
inline constexpr const char* kMenuAddWrapChunk =
    "if Menu_Add ~= nil and ttmod_orig_Add == nil then "
    "ttmod_orig_Add = Menu_Add "
    "ttmod_madd_n = 0 "
    "Menu_Add = function(widget, id, label, cb, ...) "
    "if Menu_Main_AppendLog ~= nil then "
    "if id == 'play' then Menu_Main_AppendLog('row-play') ttmod_appended = nil "
    "elseif id == 'store' then Menu_Main_AppendLog('row-store') "
    "elseif id == 'accountlink' then Menu_Main_AppendLog('row-accountlink') "
    "elseif id == 'savesFiles' then Menu_Main_AppendLog('row-savesFiles') "
    "elseif id == 'achievements' then Menu_Main_AppendLog('row-achievements') "
    "elseif id == 'stats' then Menu_Main_AppendLog('row-stats') "
    "elseif id == 'settings' then Menu_Main_AppendLog('row-settings') "
    "elseif id == 'help' then Menu_Main_AppendLog('row-help') "
    "elseif id == 'exit' then Menu_Main_AppendLog('row-exit') "
    "elseif id == 'feed' then Menu_Main_AppendLog('row-feed') "
    "end "
    "end "
    "if not ttmod_appended and id == 'exit' then "
    "ttmod_appended = true "
    "if cb ~= nil and Menu_Main_AppendLog ~= nil then Menu_Main_AppendLog(cb) end "
    "local __b = ttmod_orig_Add(ListButton, 'mods', 'label_help', 'if Menu_Mods then Menu_Mods() end') "
    "if pcall ~= nil and __b ~= nil then "
    "pcall(function() "
    "local __l = Clone_Find(__b.agent or __b, 'label') "
    "if __l ~= nil then AgentSetProperty(__l, 'Text String', 'Mods') end "
    "end) "
    "end "
    "if Menu_Main_AppendLog ~= nil then Menu_Main_AppendLog('mods-appended') end "
    "end "
    "return ttmod_orig_Add(widget, id, label, cb, ...) "
    "end "
    "end";
// ONE chunk runner: balanced-stack + error-sink discipline shared by the
// Menu_Add wrapper offer, the plugin chunk queue drain, and the menu
// refresh/ui-defs runs. Callers pass their own ABI fns; failures log.
void bridge_run_chunk(lua_State* L, LuaLoadstringFn loadstring, LuaPcallkFn pcallk,
                      LuaGettopFn gettop, LuaSetglobalFn setglobal, const char* what,
                      const char* chunk);
} // namespace ttmod_win
#endif
