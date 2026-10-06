#include "ttmod/cache.hpp"
#include "ttmod/file_io.hpp"
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

    // Transactional replacement: a bad package must NOT destroy the live
    // cache. Sync good content, then point a fresh sync at a corrupt
    // package for the same id and verify the old tree survives.
    {
        auto good = ttmod::sync_package_cache(cache, {{"c.mod", std::string(kD) + "/m.ttmod"}});
        assert(good.ok());
        assert(good.value().effective.count("c.mod"));
        FILE* f1 = fopen((cache + "/c.mod/files/a.txt").c_str(), "rb");
        assert(f1);
        fclose(f1);
        // Corrupt package for the same id: extraction fails.
        wfile(std::string(kD) + "/bad.ttmod", "not a zip at all");
        auto bad = ttmod::sync_package_cache(cache, {{"c.mod", std::string(kD) + "/bad.ttmod"}});
        assert(bad.ok());                      // per-mod failure is logged, not fatal
        assert(bad.value().effective.empty()); // not usable this round
        // The previous cache is still on disk and intact.
        FILE* f2 = fopen((cache + "/c.mod/files/a.txt").c_str(), "rb");
        assert(f2); // live cache survived the failed replacement
        fclose(f2);
        // And a later good sync brings it back.
        auto again = ttmod::sync_package_cache(cache, {{"c.mod", std::string(kD) + "/m.ttmod"}});
        assert(again.ok() && again.value().effective.count("c.mod"));
    }

    // Crash recovery: a crash between "retire old" and "install new" leaves
    // <id>.ttmod-old as the only good copy. The next sync restores it.
    {
        std::string live = cache + "/rec.mod";
        // Build a cache-shaped tree by hand (write_file_atomic will not
        // create intermediate directories).
        assert(system(("mkdir -p " + live + "/files").c_str()) == 0);
        assert(ttmod::file_io::write_file_atomic(live + "/files/a.txt", "v1"));
        assert(ttmod::file_io::write_file_atomic(live + "/.ttmod-cache", "v=1 test"));
        std::string old = cache + "/rec.mod.ttmod-old";
        assert(system(("rm -rf " + old).c_str()) == 0);
        assert(system(("mv " + live + " " + old).c_str()) == 0);
        assert(!ttmod::file_io::exists(live));
        assert(ttmod::file_io::exists(old + "/files/a.txt"));
        auto r = ttmod::sync_package_cache(cache, {});
        assert(r.ok());
        // The sweep restores the orphaned copy before anything else, and
        // says so. (Stale cleanup then removes it again: it carries our
        // marker but has no source package in this round.)
        bool restored = false;
        for (auto& l : r.value().log)
            if (l.find("rec.mod: cache restored after crash") != std::string::npos) restored = true;
        assert(restored);
    }

    // Staging leftovers from a killed process are swept, never trusted.
    {
        std::string stage = cache + "/sweep.mod.ttmod-new";
        assert(
            system(("rm -rf " + stage + " && mkdir -p " + stage + " && echo junk > " + stage + "/files.txt").c_str()) ==
            0);
        auto r = ttmod::sync_package_cache(cache, {});
        assert(r.ok());
        assert(!ttmod::file_io::exists(stage + "/files.txt"));
    }
    std::puts("cache: all asserts passed");
    return 0;
}
