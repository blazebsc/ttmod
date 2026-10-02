#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ttmod {

// Pattern like "48 8B ?? 90" — "??" or "?" is wildcard. Returns byte offsets of matches.
struct Signature {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask; // true = must match
};

std::optional<Signature> parse_signature(const std::string& text);
std::vector<size_t> scan(const uint8_t* data, size_t len, const Signature& sig);

} // namespace ttmod
