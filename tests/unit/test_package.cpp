// M11 package tests: valid round-trip, determinism, adversarial zips,
// validation rules. Malicious archives are hand-crafted (raw builder below)
// so no writer sanitization can hide behavior.
#include "ttmod/package.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "miniz.h"

namespace {

const char* kDir = "/tmp/opencode_ttmod_pkgtest";

void wfile(const std::string& p, const std::string& data) {
    FILE* f = fopen(p.c_str(), "wb");
    assert(f);
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

void setup_src() {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/src/files %s/src/plugins %s/src/.git %s/src/build", kDir, kDir,
             kDir, kDir, kDir);
    assert(system(cmd) == 0);
    wfile(std::string(kDir) + "/src/manifest.json",
          "{\"id\":\"pkg.test\",\"version\":\"1.0.0\",\"api\":1,\"files\":{\"a.txt\":\"files/a.txt\"}}");
    wfile(std::string(kDir) + "/src/files/a.txt", "hello mod");
    wfile(std::string(kDir) + "/src/plugins/p.dll", "MZfakelib");
    wfile(std::string(kDir) + "/src/.git/evil", "x");
    wfile(std::string(kDir) + "/src/build/tmp.o", "x");
}

// Minimal stored-file ZIP writer: full control over names/attrs.
struct RawZip {
    std::string buf;
    struct C {
        std::string name;
        mz_uint32 crc, size, off, ext;
    };
    std::vector<C> central;
    void add(const std::string& name, const std::string& data, mz_uint32 extattr = 0) {
        mz_uint32 crc = mz_crc32(MZ_CRC32_INIT, (const mz_uint8*)data.data(), data.size());
        auto put32 = [&](mz_uint32 v) {
            buf += (char)(v & 0xFF);
            buf += (char)((v >> 8) & 0xFF);
            buf += (char)((v >> 16) & 0xFF);
            buf += (char)((v >> 24) & 0xFF);
        };
        auto put16 = [&](mz_uint32 v) {
            buf += (char)(v & 0xFF);
            buf += (char)((v >> 8) & 0xFF);
        };
        mz_uint32 off = (mz_uint32)buf.size();
        put32(0x04034b50);
        put16(20);
        put16(0x0800); // UTF-8 flag
        put16(0);
        put16(0);
        put16(0);
        put32(crc);
        put32((mz_uint32)data.size());
        put32((mz_uint32)data.size());
        put16((mz_uint32)name.size());
        put16(0);
        buf += name;
        buf += data;
        central.push_back({name, crc, (mz_uint32)data.size(), off, extattr});
    }
    std::string finish() {
        auto put32 = [&](mz_uint32 v) {
            buf += (char)(v & 0xFF);
            buf += (char)((v >> 8) & 0xFF);
            buf += (char)((v >> 16) & 0xFF);
            buf += (char)((v >> 24) & 0xFF);
        };
        auto put16 = [&](mz_uint32 v) {
            buf += (char)(v & 0xFF);
            buf += (char)((v >> 8) & 0xFF);
        };
        mz_uint32 cdoff = (mz_uint32)buf.size();
        for (auto& c : central) {
            put32(0x02014b50);
            put16(20);
            put16(20);
            put16(0x0800);
            put16(0);
            put16(0);
            put16(0);
            put32(c.crc);
            put32(c.size);
            put32(c.size);
            put16((mz_uint32)c.name.size());
            put16(0);
            put16(0);
            put16(0);
            put16(0);
            put32(c.ext);
            put32(c.off);
            buf += c.name;
        }
        mz_uint32 cdsize = (mz_uint32)buf.size() - cdoff;
        put32(0x06054b50);
        put16(0);
        put16(0);
        put16((mz_uint32)central.size());
        put16((mz_uint32)central.size());
        put32(cdsize);
        put32(cdoff);
        put16(0);
        return buf;
    }
};

const char* kManifest = "{\"id\":\"pkg.test\",\"version\":\"1.0.0\",\"api\":1,\"files\":{\"a.txt\":\"files/a.txt\"}}";

void wzip(const std::string& p, const std::string& data) {
    wfile(p, data);
}

std::string raw_pkg(std::vector<std::pair<std::string, std::string>> files,
                    std::vector<std::pair<std::string, mz_uint32>> attrs = {}) {
    RawZip z;
    for (auto& [n, d] : files) {
        mz_uint32 ext = 0;
        for (auto& [an, ae] : attrs)
            if (an == n) ext = ae;
        z.add(n, d, ext);
    }
    return z.finish();
}

} // namespace

int main() {
#ifdef _WIN32
    _putenv("TZ=UTC");
#else
    setenv("TZ", "UTC", 1);
    tzset();
#endif
    setup_src();
    std::string src = std::string(kDir) + "/src";
    std::string p1 = std::string(kDir) + "/a.ttmod";
    std::string p2 = std::string(kDir) + "/b.ttmod";

    // Valid create + inspect (junk excluded)
    assert(ttmod::create_package(src, p1).ok());
    auto insp = ttmod::inspect_package(p1);
    assert(insp.ok());
    auto v = insp.value();
    assert(v.files.size() == 2); // files/a.txt + plugins/p.dll (.git/build out)
    assert(!v.manifest_text.empty());

    // Deterministic bytes
    assert(ttmod::create_package(src, p2).ok());
    FILE *f1 = fopen(p1.c_str(), "rb"), *f2 = fopen(p2.c_str(), "rb");
    assert(f1 && f2);
    std::string d1, d2;
    char b[4096];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f1)) > 0) d1.append(b, r);
    while ((r = fread(b, 1, sizeof b, f2)) > 0) d2.append(b, r);
    fclose(f1);
    fclose(f2);
    assert(d1 == d2);

    // Extract round-trip
    assert(ttmod::extract_package(p1, std::string(kDir) + "/out").ok());
    FILE* fa = fopen((std::string(kDir) + "/out/files/a.txt").c_str(), "rb");
    assert(fa);
    char ab[16] = {};
    fread(ab, 1, 9, fa);
    fclose(fa);
    assert(std::string(ab) == "hello mod");
    // Non-empty dest refused
    assert(!ttmod::extract_package(p1, std::string(kDir) + "/out").ok());

    // Adversarial: traversal / absolute / drive / dup / missing manifest
    wzip(std::string(kDir) + "/evil1.ttmod", raw_pkg({{"manifest.json", kManifest}, {"../evil", "x"}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil1.ttmod").ok());
    wzip(std::string(kDir) + "/evil2.ttmod", raw_pkg({{"manifest.json", kManifest}, {"C:/evil", "x"}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil2.ttmod").ok());
    wzip(std::string(kDir) + "/evil3.ttmod",
         raw_pkg({{"manifest.json", kManifest}, {"files/A.txt", "x"}, {"files/a.txt", "y"}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil3.ttmod").ok()); // casefold dup
    wzip(std::string(kDir) + "/evil4.ttmod", raw_pkg({{"files/a.txt", "x"}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil4.ttmod").ok()); // no manifest
    wzip(std::string(kDir) + "/evil5.ttmod", raw_pkg({{"manifest.json", kManifest}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil5.ttmod").ok()); // declared file missing
    // Symlink entry rejected (unix mode S_IFLNK)
    wzip(std::string(kDir) + "/evil6.ttmod",
         raw_pkg({{"manifest.json", kManifest}, {"files/a.txt", "x"}, {"link", "target"}}, {{"link", 0120000u << 16}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil6.ttmod").ok());
    // Truncated archive refused
    wzip(std::string(kDir) + "/evil7.ttmod", d1.substr(0, d1.size() / 2));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil7.ttmod").ok());
    // Future package format refused at manifest level
    wzip(std::string(kDir) + "/evil8.ttmod",
         raw_pkg({{"manifest.json", "{\"id\":\"x\",\"api\":1,\"package_format\":99,\"files\":{}}"}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil8.ttmod").ok());
    // Absurd depth rejected (raw-name bound, checked before normalization)
    std::string deep = "files";
    for (int i = 0; i < 40; ++i) deep += "/d";
    deep += "/x.txt";
    wzip(std::string(kDir) + "/evil9.ttmod", raw_pkg({{"manifest.json", kManifest}, {deep, "x"}}));
    assert(!ttmod::inspect_package(std::string(kDir) + "/evil9.ttmod").ok());

    // Package resource limits (package_policy.hpp). Each one is enforced
    // with the same canonical module, not a local constant.
    namespace packlimits = ttmod::packlimits;
    // Path length: > kMaxPathChars raw chars. Compresses to almost nothing.
    {
        std::string seg(200, 'a');
        std::string longname = "files/" + seg + "/" + seg + "/" + seg + "/x.txt"; // >512
        wzip(std::string(kDir) + "/long.ttmod", raw_pkg({{"manifest.json", kManifest}, {longname, "x"}}));
        assert(!ttmod::inspect_package(std::string(kDir) + "/long.ttmod").ok());
    }
    // Oversized manifest: > kMaxManifestBytes decompressed, tiny on disk.
    {
        std::string big = kManifest;
        big.append((size_t)ttmod::packlimits::kMaxManifestBytes, ' ');
        wzip(std::string(kDir) + "/bigman.ttmod", raw_pkg({{"manifest.json", big}}));
        auto r = ttmod::inspect_package(std::string(kDir) + "/bigman.ttmod");
        assert(!r.ok());
        assert(r.error().category == ttmod::errcat::kLimit);
    }
    // Oversized single entry: > kMaxEntryBytes decompressed (zeros compress
    // ~1000:1, so the archive stays far under kMaxPackageBytes).
    {
        std::string huge((size_t)ttmod::packlimits::kMaxEntryBytes + 1024, '\0');
        wzip(std::string(kDir) + "/huge.ttmod", raw_pkg({{"manifest.json", kManifest}, {"files/big.bin", huge}}));
        auto r = ttmod::inspect_package(std::string(kDir) + "/huge.ttmod");
        assert(!r.ok());
        assert(r.error().category == ttmod::errcat::kLimit);
    }
    // Total expansion: each entry under the per-entry cap, sum over the
    // total cap. This is the archive-bomb case the per-entry limit misses.
    {
        size_t chunk = (size_t)ttmod::packlimits::kMaxEntryBytes - 4096;
        std::string blob(chunk, '\0');
        std::vector<std::pair<std::string, std::string>> files = {{"manifest.json", kManifest}};
        for (int i = 0; i < 5; ++i) files.push_back({"files/big" + std::to_string(i) + ".bin", blob});
        wzip(std::string(kDir) + "/bomb.ttmod", raw_pkg(files));
        auto r = ttmod::inspect_package(std::string(kDir) + "/bomb.ttmod");
        assert(!r.ok());
        assert(r.error().category == ttmod::errcat::kLimit);
    }
    // Too many entries: > kMaxEntries central-directory records.
    {
        std::vector<std::pair<std::string, std::string>> files = {{"manifest.json", kManifest}};
        for (uint32_t i = 0; i < ttmod::packlimits::kMaxEntries + 8; ++i)
            files.push_back({"files/f" + std::to_string(i) + ".txt", "x"});
        wzip(std::string(kDir) + "/many.ttmod", raw_pkg(files));
        auto r = ttmod::inspect_package(std::string(kDir) + "/many.ttmod");
        assert(!r.ok());
        assert(r.error().category == ttmod::errcat::kLimit);
    }
    // A package at the limit is still accepted: the bounds must not reject
    // legitimate content. Uses a manifest with no declared files, since
    // inspect also cross-checks declared files[] against entry names.
    {
        const char* kBareManifest = "{\"id\":\"many.mod\",\"api\":1}";
        std::vector<std::pair<std::string, std::string>> files = {{"manifest.json", kBareManifest}};
        for (uint32_t i = 0; i < ttmod::packlimits::kMaxEntries - 2; ++i)
            files.push_back({"files/f" + std::to_string(i) + ".txt", "x"});
        wzip(std::string(kDir) + "/atlimit.ttmod", raw_pkg(files));
        auto r = ttmod::inspect_package(std::string(kDir) + "/atlimit.ttmod");
        assert(r.ok());
        assert(r.value().files.size() == ttmod::packlimits::kMaxEntries - 2);
    }

    std::puts("package: all asserts passed");
    return 0;
}
