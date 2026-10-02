#include "ttmod/sigmatch.hpp"
#include <cassert>
#include <cstdio>

int main() {
    // Exact match
    auto s1 = ttmod::parse_signature("48 8B 90");
    assert(s1.has_value());
    uint8_t d1[] = {0x00, 0x48, 0x8B, 0x90, 0xFF};
    auto m1 = ttmod::scan(d1, sizeof d1, *s1);
    assert(m1.size() == 1 && m1[0] == 1);
    // Wildcard + multiple matches
    auto s2 = ttmod::parse_signature("48 ?? 90");
    assert(s2.has_value());
    uint8_t d2[] = {0x48, 0x00, 0x90, 0x48, 0xFF, 0x90};
    auto m2 = ttmod::scan(d2, sizeof d2, *s2);
    assert(m2.size() == 2 && m2[0] == 0 && m2[1] == 3);
    // Bad pattern rejected
    assert(!ttmod::parse_signature("ZZ").has_value());
    assert(!ttmod::parse_signature("").has_value());
    // No match
    auto m3 = ttmod::scan(d1, sizeof d1, *s2);
    assert(m3.size() == 1);
    std::puts("sigmatch: all asserts passed");
    return 0;
}
