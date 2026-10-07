#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ttmod/result.hpp"

namespace ttmod {

// Pattern like "48 8B ?? 90" — "??" or "?" is wildcard. Returns byte offsets of matches.
struct Signature {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask; // true = must match
};

Result<Signature> parse_signature(const std::string& text);
std::vector<size_t> scan(const uint8_t* data, size_t len, const Signature& sig);

} // namespace ttmod
