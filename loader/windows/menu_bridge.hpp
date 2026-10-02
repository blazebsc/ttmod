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
} // namespace ttmod_win
#endif
