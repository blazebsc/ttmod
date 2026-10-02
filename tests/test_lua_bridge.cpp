#include "ttmod/lua_bridge.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

// .text of the verified exe: va 0x1000, vsize 0x8272cc.
static constexpr uint32_t kTextVa = 0x1000;
static constexpr uint32_t kTextSize = 0x8272cc;

static uint8_t g_mem[4];
static uint32_t g_want = 0;
static const uint8_t* reader(uint32_t rva) {
    return rva == g_want ? g_mem : nullptr;
}

int main() {
    ttmod::LuaBridgeAddrs a;
    // bounds: all verified RVAs inside .text
    assert(ttmod::lua_bridge_rvas_in_text(a, kTextVa, kTextSize));
    // bounds: each field individually OOB-safe (craft one bad copy per field)
    ttmod::LuaBridgeAddrs bad = a;
    bad.newstate = 0x900000; // past .text end (0x8282CC)
    assert(!ttmod::lua_bridge_rvas_in_text(bad, kTextVa, kTextSize));
    bad = a;
    bad.setglobal = kTextVa + kTextSize; // one past end
    assert(!ttmod::lua_bridge_rvas_in_text(bad, kTextVa, kTextSize));
    bad = a;
    bad.pcallk = kTextVa - 1; // just before
    assert(!ttmod::lua_bridge_rvas_in_text(bad, kTextVa, kTextSize));
    assert(!ttmod::lua_bridge_rvas_in_text(a, kTextVa, 0)); // empty section
    // anchors: match (unpacked-memory prologue; file bytes differ, packer)
    memcpy(g_mem, "\x55\x8B\xEC\x83", 4);
    g_want = 0x611C80;
    assert(ttmod::lua_bridge_anchor_ok(ttmod::kLuaNewstateAnchor, reader));
    memcpy(g_mem, "\x0F\x70\xCA\x56", 4);
    g_want = 0x1139F0;
    assert(ttmod::lua_bridge_anchor_ok(ttmod::kLoadResourceAnchor, reader));
    // live anchor: unpacked prologue, verified stable + disassembled
    memcpy(g_mem, "\x55\x8B\xEC\x6A", 4);
    g_want = 0x1139F0;
    assert(ttmod::lua_bridge_anchor_ok(ttmod::kLoadResourceLiveAnchor, reader));
    // anchors: mismatch paths
    memcpy(g_mem, "\x00\x38\xF7\xFC", 4);
    g_want = 0x611C80;
    assert(!ttmod::lua_bridge_anchor_ok(ttmod::kLuaNewstateAnchor, reader));
    assert(!ttmod::lua_bridge_anchor_ok(ttmod::kLuaNewstateAnchor, nullptr));
    g_want = 0xDEAD; // unreadable RVA
    assert(!ttmod::lua_bridge_anchor_ok(ttmod::kLuaNewstateAnchor, reader));
    // proof chunk sets exactly the proof global (keeps Lua surface minimal)
    assert(strstr(ttmod::kLuaBridgeTestChunk, ttmod::kLuaBridgeTestGlobal) != nullptr);
    // Game-idiom menu chunks live loader-side (loader/windows/menu_bridge.hpp);
    // core keeps only the portable validation proven above.
    std::puts("lua_bridge: all asserts passed");
    return 0;
}
