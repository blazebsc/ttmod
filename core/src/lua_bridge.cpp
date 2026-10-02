// Portable Lua-bridge validation (see header). No OS calls.
#include "ttmod/lua_bridge.hpp"

namespace ttmod {

bool lua_bridge_rvas_in_text(const LuaBridgeAddrs& a, uint32_t text_va, uint32_t text_size) {
    if (text_size == 0) return false;
    const uint32_t rvas[] = {a.newstate, a.pcallk,         a.gettop,  a.loadstring,
                             a.tolstring, a.setglobal, a.pushcclosure};
    for (uint32_t r : rvas) {
        if (r < text_va || r >= text_va + text_size) return false;
    }
    return true;
}

bool lua_bridge_anchor_ok(const LuaAnchor& anchor, const uint8_t* (*read4)(uint32_t rva)) {
    if (!read4) return false;
    const uint8_t* p = read4(anchor.rva);
    if (!p) return false;
    for (int i = 0; i < 4; ++i)
        if (p[i] != anchor.bytes[i]) return false;
    return true;
}

} // namespace ttmod
