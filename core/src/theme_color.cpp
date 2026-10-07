#include "ttmod/theme_color.hpp"

namespace ttmod {

Result<AccentRgb> parse_accent(const std::string& s) {
    auto fail = [&](const std::string& message) {
        return Result<AccentRgb>::fail(Error{"parse-accent", s, errcat::kSyntax, message});
    };
    if (s.size() != 7 || s[0] != '#') return fail("expected #RRGGBB");
    unsigned v = 0;
    for (size_t i = 1; i < 7; ++i) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return fail("non-hex digit");
    }
    AccentRgb out;
    out.r = ((v >> 16) & 255) / 255.0f;
    out.g = ((v >> 8) & 255) / 255.0f;
    out.b = (v & 255) / 255.0f;
    return Result<AccentRgb>::ok(out);
}

bool should_substitute(float r, float g, float b) {
    float scale = 1.0f;
    if (r > 1.0f || g > 1.0f || b > 1.0f) scale = 255.0f;
    float mn = r, mx = r;
    if (g < mn) mn = g;
    if (b < mn) mn = b;
    if (g > mx) mx = g;
    if (b > mx) mx = b;
    return (mx - mn) / scale < 0.05f && mn / scale >= 0.8f;
}

} // namespace ttmod
