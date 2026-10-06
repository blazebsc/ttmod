#pragma once
#include <string>
#include "ttmod/result.hpp"

namespace ttmod {

// Single source of the theme-recolour color decision (the accent rule lives
// here, not in two languages). Both the Lua paint path (which mirrors these
// vectors in tests/test_menumods_ui.py) and the native setter hook use it:
// parse the configured accent once, then test every candidate write.
//
// A write is substitutable when it is near-gray AND bright: pure white
// (hover select), stock gray ~0.878 (deselect restores stock, never the
// accent). Black fills, disabled gray (~0.4), and real tints pass through.
// Symmetric hues (white/green/blue) render identically on every path;
// asymmetric ones (orange) expose a channel-order quirk in one render path.
struct AccentRgb {
    float r = 0, g = 0, b = 0;
};

// Parse "#RRGGBB" (case-insensitive) into 0..1 floats. Fails on garbage.
Result<AccentRgb> parse_accent(const std::string& s);

// True iff (r, g, b) - in EITHER 0..1 floats or 0..255 ints - is a
// near-gray (>=0.8 after normalization) worth recolouring.
bool should_substitute(float r, float g, float b);

} // namespace ttmod
