#include "ttmod/detect.hpp"
#include <cstdio>
#include <cstring>

namespace ttmod {

static uint16_t rd16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

uint64_t fnv1a_file(const std::string& path, uint64_t* out_size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    uint64_t h = 14695981039346656037ull;
    uint64_t n = 0;
    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) {
        n += r;
        for (size_t i = 0; i < r; ++i) {
            h ^= (uint8_t)buf[i];
            h *= 1099511628211ull;
        }
    }
    fclose(f);
    if (out_size) *out_size = n;
    return h;
}

ExeInfo parse_pe(const std::string& path) {
    ExeInfo e;
    e.path = path;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { e.error = "open failed"; return e; }
    uint8_t hdr[512] = {};
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) { e.error = "too small"; fclose(f); return e; }
    fclose(f);
    if (hdr[0] != 'M' || hdr[1] != 'Z') { e.error = "no MZ"; return e; }
    uint32_t lfanew = rd32(hdr + 0x3C);
    if (lfanew > 1024) { e.error = "bad e_lfanew"; return e; }
    // Re-read PE header at lfanew
    f = fopen(path.c_str(), "rb");
    fseek(f, lfanew, SEEK_SET);
    uint8_t pe[64] = {};
    if (fread(pe, 1, sizeof pe, f) != sizeof pe) { e.error = "pe hdr short"; fclose(f); return e; }
    fclose(f);
    if (!(pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0)) { e.error = "no PE sig"; return e; }
    e.machine = rd16(pe + 4);
    e.num_sections = rd16(pe + 6);
    e.timestamp = rd32(pe + 8);
    e.opt_magic = rd16(pe + 24);
    e.file_size = 0;
    e.fnv1a64 = fnv1a_file(path, &e.file_size);
    e.ok = true;
    return e;
}

} // namespace ttmod
