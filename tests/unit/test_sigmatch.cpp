#include "ttmod/sigmatch.hpp"
#include <cassert>
#include <cstdio>

int main() {
    // Exact match
    auto s1 = ttmod::parse_signature("48 8B 90");
    assert(s1.ok());
    uint8_t d1[] = {0x00, 0x48, 0x8B, 0x90, 0xFF};
    auto m1 = ttmod::scan(d1, sizeof d1, s1.value());
    assert(m1.size() == 1 && m1[0] == 1);
    // Wildcard + multiple matches
    auto s2 = ttmod::parse_signature("48 ?? 90");
    assert(s2.ok());
    uint8_t d2[] = {0x48, 0x00, 0x90, 0x48, 0xFF, 0x90};
    auto m2 = ttmod::scan(d2, sizeof d2, s2.value());
    assert(m2.size() == 2 && m2[0] == 0 && m2[1] == 3);
    // Bad pattern rejected
    auto bad = ttmod::parse_signature("ZZ");
    assert(!bad.ok() && bad.error().category == ttmod::errcat::kSyntax);
    assert(bad.error().operation == "parse-signature" && bad.error().message.find("ZZ") != std::string::npos);
    auto missing = ttmod::parse_signature("");
    assert(!missing.ok() && missing.error().category == ttmod::errcat::kMissing);
    assert(missing.error().operation == "parse-signature");
    // No match
    auto m3 = ttmod::scan(d1, sizeof d1, s2.value());
    assert(m3.size() == 1);
    std::puts("sigmatch: all asserts passed");
    return 0;
}
