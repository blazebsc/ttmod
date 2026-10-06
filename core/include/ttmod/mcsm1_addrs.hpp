#pragma once
// Hook/setter addresses owned by the mcsm1_pc_x86 profile (Stage F).
// Lua VM + bridge addresses live in core/lua_bridge.hpp (historical);
// this file holds the property-system hook targets found by static
// analysis. All values are RVAs (live base + RVA); every hook validates
// its anchor bytes in live memory before MinHook installs, and installs
// nothing on mismatch. Single owner: add new hook targets here, never
// as literals in loader code.
#include <cstddef>
#include <cstdint>

namespace ttmod {

// Engine color setter (thiscall ecx=this, ret $0xc, args
// descriptor/color-struct/flag). The theme substitute hooks it.
inline constexpr uint32_t kScolRva = 0x168430;
inline constexpr uint8_t kScolAnchor[10] = {0x55, 0x8B, 0xEC, 0x51, 0x56,
                                            0x57, 0x8B, 0xF1, 0xE8, 0xD3};

} // namespace ttmod
