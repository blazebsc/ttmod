// MCSM1 runtime Lua bridge (additive milestone). Clean-room implementation:
// hook the game's own lua_newstate (late install — early MinHook on game
// code hangs Wine, see hooks.cpp), capture the live lua_State*, then run
// ONE harmless framework-owned chunk on it as execution proof.
//
// Thread safety: the detour + proof run synchronously on the game's own
// lua_newstate caller thread, against a freshly created state that no
// script is running on yet. The bridge NEVER calls Lua from a framework
// thread and never touches the state after the detour returns.
// Stack: proof uses nresults=0 and gettop before/after must match.
// Surface: the raw lua_State* stays framework-internal (no ABI exposure).
#pragma once
#ifdef _WIN32
#include "ttmod/runtime_owner.hpp"
namespace ttmod_win {
void lua_bridge_init(const char* profile_id, const char* log_path, ttmod::RuntimeOwner* owner);
void lua_bridge_shutdown();
// Picker glow gate (ttmod_menu_palette calls this): while the colour picker
// is the active screen, the engine's GLOBAL light struct (the hover glow
// source, kScolLightStructRva) is set to BLACK instead of the accent, so
// both build-time and hover-time styling write a glow that adds nothing and
// the per-row swatch colours render pure. On close the accent is restored
// (or stock white when no accent is configured).
void lua_bridge_set_light_palette(bool picker_open);
} // namespace ttmod_win
#endif
