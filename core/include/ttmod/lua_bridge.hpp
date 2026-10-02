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

// Game-idiom menu chunks live loader-side in loader/windows/menu_bridge.hpp
// (loader-owned); core keeps only portable validation below.
// Framework-owned proof global (new name; never collides with game globals).
inline constexpr const char* kLuaBridgeTestGlobal = "ttmod_bridge_ok";
inline constexpr const char* kLuaBridgeTestChunk = "ttmod_bridge_ok = true";

} // namespace ttmod
