#include "ttmod/cache.hpp"
#include "ttmod/package.hpp"
#include <cassert>
#include <cstdio>
#include <string>

namespace {

const char* kD = "/tmp/opencode_ttmod_cachetest";

void wfile(const std::string& p, const std::string& data) {
    FILE* f = fopen(p.c_str(), "wb");
    assert(f);
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

} // namespace

int main() {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/src/m/files", kD, kD);
    assert(system(cmd) == 0);
    wfile(std::string(kD) + "/src/m/manifest.json",
          "{\"id\":\"c.mod\",\"version\":\"1.0.0\",\"api\":1,\"files\":{\"a.txt\":\"files/a.txt\"}}");
    wfile(std::string(kD) + "/src/m/files/a.txt", "v1");
    assert(ttmod::create_package(std::string(kD) + "/src/m", std::string(kD) + "/m.ttmod").ok());
    std::string cache = std::string(kD) + "/cache";

    // First sync extracts.
    auto s1r = ttmod::sync_package_cache(cache, {{"c.mod", std::string(kD) + "/m.ttmod"}});
    assert(s1r.ok());
    auto s1 = s1r.value();
    assert(s1.effective.count("c.mod"));
    assert(s1.effective["c.mod"] == cache + "/c.mod");
    FILE* f = fopen((cache + std::string("/c.mod/files/a.txt")).c_str(), "rb");
    assert(f);
    char b[8] = {};
    fread(b, 1, 2, f);
    fclose(f);
    assert(std::string(b) == "v1");

    // Second sync: fresh, no re-extract (no "cached from package" line).
    auto s2r = ttmod::sync_package_cache(cache, {{"c.mod", std::string(kD) + "/m.ttmod"}});
    assert(s2r.ok());
    auto s2 = s2r.value();
    for (auto& l : s2.log) assert(l.find("cached from package") == std::string::npos);

    // Stale cleanup: unknown dir without marker survives; ours without source dies.
    char cmd2[1024];
    snprintf(cmd2, sizeof cmd2, "mkdir -p %s/foreign && touch %s/foreign/x", cache.c_str(), cache.c_str());
    assert(system(cmd2) == 0);
    auto s3r = ttmod::sync_package_cache(cache, {});
    assert(s3r.ok() && s3r.value().effective.empty());
    FILE* ff = fopen((cache + std::string("/foreign/x")).c_str(), "rb");
    assert(ff); // untouched
    fclose(ff);
    FILE* gone = fopen((cache + std::string("/c.mod/files/a.txt")).c_str(), "rb");
    assert(!gone); // stale removed

    // Missing package file: skipped with log, ok stays true.
    auto s4r = ttmod::sync_package_cache(cache, {{"ghost", std::string(kD) + "/nope.ttmod"}});
    assert(s4r.ok() && s4r.value().effective.empty());
    std::puts("cache: all asserts passed");
    return 0;
}
