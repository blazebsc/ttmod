#pragma once
// Portable validation for the MCSM1 runtime Lua bridge (additive milestone).
// Pure logic, no OS calls: the Windows detour layer consumes this.
// Addresses are RVAs for the mcsm1_pc_x86 profile (verified read-only
// against exe md5 171ff4fe…; see docs/research/runtime/lua-pipeline.md).
// License boundary: clean-room declarations; no third-party code.
#include <cstddef>
#include <cstdint>

namespace ttmod {

// Verified function RVAs (lua_newstate/loadresource byte-anchored; the rest
// bounds-checked inside .text).
struct LuaBridgeAddrs {
    uint32_t newstate = 0x611C80;
    uint32_t pcallk = 0x60D3B0;
    uint32_t gettop = 0x60B860;
    uint32_t loadstring = 0x60EBF0;
    uint32_t tolstring = 0x60C310;
    uint32_t setglobal = 0x60CDD0;
    // Menu-bridge functions: same offset family; proven live by the
    // vocabulary-probe runs (C-function registration + calls succeeded).
    uint32_t pushcclosure = 0x60C850;
};

// Byte anchors. The exe is packed: FILE bytes at lua_newstate are
// 8A 38 F7 FC, but live (unpacked, >=2.5s) bytes are the real prologue
// 55 8B EC 83. The bridge validates LIVE memory, so it anchors on the
// unpacked prologue; the LoadResource anchor is init-time packed bytes
// (diagnostic only, never a hook target).
struct LuaAnchor {
    uint32_t rva;
    uint8_t bytes[4];
};

inline constexpr LuaAnchor kLuaNewstateAnchor{0x611C80, {0x55, 0x8B, 0xEC, 0x83}};
inline constexpr LuaAnchor kLoadResourceAnchor{0x1139F0, {0x0F, 0x70, 0xCA, 0x56}};
// Live (unpacked) prologue at the LoadResource RVA: standard MSVC frame
// (push ebp; mov ebp,esp; push -1 SEH sentinel). Stack-arg function
// (ebp+8/ebp+0xc), cdecl-compatible detour. VERIFIED: stable across
// retries in live runs AND matches static disassembly of unpacked .text.
inline constexpr LuaAnchor kLoadResourceLiveAnchor{0x1139F0, {0x55, 0x8B, 0xEC, 0x6A}};

// True iff every bridge RVA lands inside [text_va, text_va + text_size).
bool lua_bridge_rvas_in_text(const LuaBridgeAddrs& a, uint32_t text_va, uint32_t text_size);

// True iff the 4 bytes at the anchor RVA equal the expected bytes.
// read4(rva) must return a pointer to 4 readable bytes, or nullptr.
bool lua_bridge_anchor_ok(const LuaAnchor& anchor, const uint8_t* (*read4)(uint32_t rva));

// Framework-owned proof global (new name; never collides with game globals).
inline constexpr const char* kLuaBridgeTestGlobal = "ttmod_bridge_ok";
inline constexpr const char* kLuaBridgeTestChunk = "ttmod_bridge_ok = true";

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

// Menu_Mods entry point (idempotent: safe to run on every captured state).
// Markers distinguish exists (type(Menu_Mods)=="function"), called
// (ttmod_mods_calls increments), returned (chunk pcall==0 in harness).
// The guarded Menu_Options transition reuses the proven-safe target.
inline constexpr const char* kMenuModsFn = "Menu_Mods";
inline constexpr const char* kMenuModsCalls = "ttmod_mods_calls";
inline constexpr const char* kMenuModsPressed = "ttmod_mods_pressed";
inline constexpr const char* kMenuModsChunk =
    "function Menu_Mods() "
    "ttmod_mods_calls = (ttmod_mods_calls or 0) + 1 "
    "ttmod_mods_pressed = true "
    "if Menu_Options then Menu_Options() end "
    "end";

} // namespace ttmod
