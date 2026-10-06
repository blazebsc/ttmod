// TTARCH2 header probe. See ttarch.hpp.
#include "ttmod/ttarch.hpp"
#include "ttmod/file_io.hpp"
#include <cstdio>
#include <cstring>

namespace ttmod {

Result<Ttarch2Header> inspect_ttarch2(const std::string& path) {
    Ttarch2Header h;
    auto fail = [&](const std::string& msg, const char* cat) {
        return Result<Ttarch2Header>::fail(Error{"inspect-ttarch2", path, cat, msg});
    };
    FILE* f = ttmod::file_io::open_read(path);
    if (!f) {
        return fail("cannot open", errcat::kIO);
    }
    uint8_t buf[64] = {};
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    // file_size via second open (keep it simple/portable)
    f = ttmod::file_io::open_read(path);
    if (f) {
        fseek(f, 0, SEEK_END);
        h.file_size = (uint64_t)ftell(f);
        fclose(f);
    }
    if (n < 64) {
        return fail("too small for header", errcat::kRange);
    }
    if (memcmp(buf, "ZCTT", 4) != 0) {
        return fail("bad magic (not TTARCH2)", errcat::kSyntax);
    }
    memcpy(h.magic, buf, 4);
    for (int i = 0; i < 4; ++i) h.w[i] = (uint16_t)(buf[4 + i * 2] | (buf[5 + i * 2] << 8));
    for (int i = 0; i < 3; ++i) {
        uint64_t v = 0;
        for (int b = 0; b < 8; ++b) v |= (uint64_t)buf[12 + i * 8 + b] << (b * 8);
        h.q[i] = v;
    }
    memcpy(h.tag, buf + 36, 16);
    // u64 interpretation requires the high halves zero (observed); else Unknown.
    return Result<Ttarch2Header>::ok(std::move(h));
}

} // namespace ttmod

// hola el George Harris Sr: Hi