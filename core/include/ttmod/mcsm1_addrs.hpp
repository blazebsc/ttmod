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
inline constexpr uint8_t kScolAnchor[10] = {0x55, 0x8B, 0xEC, 0x51, 0x56, 0x57, 0x8B, 0xF1, 0xE8, 0xD3};
// Its sibling property GETTER (0x1684B0, 0x80 below the setter). ABI
// verified from the unpacked dump (mcsm-unpacked.bin, 2026-10-07), not
// guessed: thiscall ecx=this, ret $0xc, stack args
// [ebp+8]=descriptor, [ebp+0xC]=out colour (16 bytes: r,g,b,a floats),
// [ebp+0x10]=flag (same slot selector the setter takes). Returns al=1 on
// success. Proven in-game to decline our descriptors (got=0 on every call,
// both flags, forced ecx) - the flag mapping only yields slots {1,4} while
// the live colour lives in slot 2. probe_slot in lua_bridge.cpp reads that
// slot directly instead; this constant documents the negative result so the
// getter is not re-derived.
inline constexpr uint32_t kScolBRva = 0x1684B0;
inline constexpr uint8_t kScolBAnchor[10] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x8B, 0x45, 0x10, 0x48};
// Property-store lookup called by the scol setter (push 0x2) and the engine
// getter: resolves (descriptor, slot) to a live entry. Selector 2 is the
// slot the setter itself writes (verified in the setter disasm at
// 0x168449-0x16845c); the getter's flag mapping can never produce it.
// Contract: ecx=self, 4 stack args (desc, &lo, &hi, sel), ret 0x10 (cleans
// its own args). Anchor is the function prologue.
inline constexpr uint32_t kPropLookupRva = 0x2B150;
inline constexpr uint8_t kPropLookupAnchor[12] = {0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56,
                                                  0x57, 0x8B, 0xF9, 0x89, 0x7D, 0xFC};
// Context resolve called by both paths with no stack args (plain ret).
// NOTE: its prologue contains an absolute address (a1 b4 95 dd 00), so this
// anchor pins the non-rebased image - consistent with the exe_base=00400000
// invariant every other anchor already assumes.
inline constexpr uint32_t kPropCtxRva = 0x23110;
inline constexpr uint8_t kPropCtxAnchor[12] = {0xA1, 0xB4, 0x95, 0xDD, 0x00, 0xA9,
                                               0x00, 0x00, 0x00, 0x20, 0x75, 0x7F};

} // namespace ttmod
