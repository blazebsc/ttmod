#include "ttmod/theme_color.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using ttmod::parse_accent;
using ttmod::should_substitute;

static bool near(float a, float b) {
    return std::fabs(a - b) < 1e-6f;
}

int main() {
    // Golden vectors. tests/test_menumods_ui.py asserts these SAME values
    // through the Lua path (see "golden" there); change both together.
    auto acc = parse_accent("#FF8000");
    assert(acc && near(acc->r, 1.0f) && near(acc->g, 128 / 255.0f) && near(acc->b, 0.0f));
    acc = parse_accent("#00ff00");
    assert(acc && near(acc->r, 0.0f) && near(acc->g, 1.0f) && near(acc->b, 0.0f));
    acc = parse_accent("#0080FF");
    assert(acc && near(acc->r, 0.0f) && near(acc->g, 128 / 255.0f) && near(acc->b, 1.0f));
    assert(!parse_accent("blue"));
    assert(!parse_accent("#FFF"));
    assert(!parse_accent("#GGGGGG"));
    assert(!parse_accent(""));
    // Floats: white / stock gray substitute; black / disabled / tints pass.
    assert(should_substitute(1, 1, 1));
    assert(should_substitute(0.878f, 0.878f, 0.878f));
    assert(!should_substitute(0, 0, 0));
    assert(!should_substitute(0.4f, 0.4f, 0.4f));
    assert(!should_substitute(1, 1, 0.6f));   // pale yellow: real tint
    assert(!should_substitute(1, 0.502f, 0)); // the accent itself
    // 0..255 ints take the same rule.
    assert(should_substitute(255, 255, 255));
    assert(should_substitute(224, 224, 224));
    assert(!should_substitute(102, 102, 102));
    assert(!should_substitute(255, 128, 0));
    printf("themecolor: golden vectors OK\n");
    return 0;
}
