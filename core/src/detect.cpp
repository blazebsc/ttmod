#include "ttmod/detect.hpp"
#include "ttmod/file_io.hpp"
#include <cstdio>
#include <cstring>

namespace ttmod {

static uint16_t rd16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

Result<FileDigest> fnv1a_file(const std::string& path) {
    FILE* f = ttmod::file_io::open_read(path);
    if (!f) return Result<FileDigest>::fail(Error{"hash-file", path, errcat::kIO, "cannot open file"});
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
    if (ferror(f)) {
        fclose(f);
        return Result<FileDigest>::fail(Error{"hash-file", path, errcat::kIO, "cannot read file"});
    }
    fclose(f);
    return Result<FileDigest>::ok(FileDigest{h, n});
}

Result<ExeInfo> parse_pe(const std::string& path) {
    ExeInfo e;
    e.path = path;
    auto fail = [&](const std::string& msg, const char* cat) {
        return Result<ExeInfo>::fail(Error{"parse-pe", path, cat, msg});
    };
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return fail("open failed", errcat::kIO);
    uint8_t hdr[512] = {};
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) {
        fclose(f);
        return fail("too small", errcat::kRange);
    }
    fclose(f);
    // Identity needs the whole file (hash); headers parse from the prefix.
    auto digest = fnv1a_file(path);
    if (!digest.ok()) return Result<ExeInfo>::fail(digest.error());
    e.file_size = digest.value().size;
    e.fnv1a64 = digest.value().fnv1a64;
    auto h = parse_pe_bytes(std::span<const std::byte>((const std::byte*)hdr, sizeof hdr));
    if (!h.ok()) return h;
    e.machine = h.value().machine;
    e.num_sections = h.value().num_sections;
    e.timestamp = h.value().timestamp;
    e.opt_magic = h.value().opt_magic;
    return Result<ExeInfo>::ok(std::move(e));
}

Result<ExeInfo> parse_pe_bytes(std::span<const std::byte> bytes) {
    ExeInfo e;
    auto fail = [&](const std::string& msg, const char* cat) {
        return Result<ExeInfo>::fail(Error{"parse-pe", "", cat, msg});
    };
    if (bytes.size() < 512) return fail("too small", errcat::kRange);
    const uint8_t* hdr = (const uint8_t*)bytes.data();
    if (hdr[0] != 'M' || hdr[1] != 'Z') return fail("no MZ", errcat::kSyntax);
    uint32_t lfanew = rd32(hdr + 0x3C);
    if (lfanew > 1024 || lfanew + 64 > bytes.size()) return fail("bad e_lfanew", errcat::kSyntax);
    const uint8_t* pe = hdr + lfanew;
    if (!(pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0))
        return fail("no PE sig", errcat::kSyntax);
    e.machine = rd16(pe + 4);
    e.num_sections = rd16(pe + 6);
    e.timestamp = rd32(pe + 8);
    e.opt_magic = rd16(pe + 24);
    return Result<ExeInfo>::ok(std::move(e));
}

} // namespace ttmod
