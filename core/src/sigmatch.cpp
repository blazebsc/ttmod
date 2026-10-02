#include "ttmod/sigmatch.hpp"
#include <cctype>
#include <sstream>

namespace ttmod {

std::optional<Signature> parse_signature(const std::string& text) {
    Signature s;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        if (tok == "?" || tok == "??") {
            s.bytes.push_back(0);
            s.mask.push_back(false);
        } else {
            if (tok.size() != 2 || !isxdigit((unsigned char)tok[0]) || !isxdigit((unsigned char)tok[1]))
                return std::nullopt;
            s.bytes.push_back((uint8_t)strtoul(tok.c_str(), nullptr, 16));
            s.mask.push_back(true);
        }
    }
    if (s.bytes.empty()) return std::nullopt;
    return s;
}

std::vector<size_t> scan(const uint8_t* data, size_t len, const Signature& sig) {
    std::vector<size_t> out;
    size_t n = sig.bytes.size();
    if (n == 0 || n > len) return out;
    for (size_t i = 0; i + n <= len; ++i) {
        bool ok = true;
        for (size_t j = 0; j < n; ++j) {
            if (sig.mask[j] && data[i + j] != sig.bytes[j]) { ok = false; break; }
        }
        if (ok) out.push_back(i);
    }
    return out;
}

} // namespace ttmod
