#include "ttmod/discovery.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/package.hpp"
#include <cassert>
#include <cstdio>
#include <string>

namespace {

const char* kD = "/tmp/opencode_ttmod_discovery/mods";

void wfile(const std::string& p, const std::string& data) {
    FILE* f = fopen(p.c_str(), "wb");
    assert(f);
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

void setup() {
    char cmd[512];
    snprintf(cmd, sizeof cmd,
             "rm -rf /tmp/opencode_ttmod_discovery && mkdir -p %s/devmod/files "
             "/tmp/opencode_ttmod_discovery/src/pkgdir/files",
             kD);
    assert(system(cmd) == 0);
    wfile(std::string(kD) + "/devmod/manifest.json",
          "{\"id\":\"dev.mod\",\"version\":\"1.0.0\",\"api\":1,\"games\":[\"minecraft-story-mode:s1\"],"
          "\"files\":{\"a.txt\":\"files/a.txt\"}}");
    wfile(std::string(kD) + "/devmod/files/a.txt", "dev");
    // package mod (same content, different id) via create_package
    wfile("/tmp/opencode_ttmod_discovery/src/pkgdir/manifest.json",
          "{\"id\":\"pkg.mod\",\"version\":\"2.0.0\",\"api\":1,\"games\":[\"minecraft-story-mode:s1\"],"
          "\"files\":{\"a.txt\":\"files/a.txt\"}}");
    wfile("/tmp/opencode_ttmod_discovery/src/pkgdir/files/a.txt", "pkg");
    assert(ttmod::create_package("/tmp/opencode_ttmod_discovery/src/pkgdir",
                                 std::string(kD) + "/pkgmod.ttmod")
               .ok());
    // junk: ignored silently
    wfile(std::string(kD) + "/README.txt", "hi");
    wfile(std::string(kD) + "/random.dll", "MZ");
    wfile(std::string(kD) + "/other.zip", "PK");
    wfile(std::string(kD) + "/.hidden", "x");
    // broken package
    wfile(std::string(kD) + "/broken.ttmod", "not a zip");
    // unpacked dir without manifest: ignored
    char cmd2[512];
    snprintf(cmd2, sizeof cmd2, "mkdir -p %s/screenshots", kD);
    assert(system(cmd2) == 0);
    // bad manifest dir + wrong game + bad api
    snprintf(cmd2, sizeof cmd2, "mkdir -p %s/bad %s/othergame %s/badapi", kD, kD, kD);
    assert(system(cmd2) == 0);
    wfile(std::string(kD) + "/bad/manifest.json", "{oops");
    wfile(std::string(kD) + "/othergame/manifest.json",
          "{\"id\":\"g.mod\",\"api\":1,\"games\":[\"other-game:s9\"]}");
    wfile(std::string(kD) + "/badapi/manifest.json", "{\"id\":\"a.mod\",\"api\":99}");
    // duplicate id: dir wins over package
    snprintf(cmd2, sizeof cmd2, "mkdir -p %s/dupdir/files /tmp/opencode_ttmod_discovery/src/dupsrc/files", kD);
    assert(system(cmd2) == 0);
    wfile(std::string(kD) + "/dupdir/manifest.json",
          "{\"id\":\"dup.mod\",\"api\":1,\"games\":[\"minecraft-story-mode:s1\"],\"files\":{}}");
    wfile("/tmp/opencode_ttmod_discovery/src/dupsrc/manifest.json",
          "{\"id\":\"dup.mod\",\"api\":1,\"games\":[\"minecraft-story-mode:s1\"],\"files\":{}}");
    wfile("/tmp/opencode_ttmod_discovery/src/dupsrc/files/a.txt", "x");
    assert(ttmod::create_package("/tmp/opencode_ttmod_discovery/src/dupsrc",
                                 std::string(kD) + "/dup.ttmod")
               .ok());
}

bool has_id(const ttmod::Discovery& d, const std::string& id, bool* packaged = nullptr) {
    for (auto& m : d.mods)
        if (m.id.str() == id) {
            if (packaged) *packaged = m.packaged;
            return true;
        }
    return false;
}

bool skipped_has(const ttmod::Discovery& d, const std::string& sub) {
    for (auto& s : d.skipped)
        if (s.find(sub) != std::string::npos) return true;
    return false;
}

} // namespace

int main() {
    setup();
    ttmod::ModState st;
    auto d = ttmod::discover_mods(kD, st, "minecraft-story-mode", 1);
    bool pkg = false;
    assert(has_id(d, "dev.mod") && has_id(d, "pkg.mod", &pkg) && pkg);
    assert(!has_id(d, "g.mod") && !has_id(d, "a.mod"));
    auto invalid_has = [&](const std::string& sub) {
        for (auto& e : d.invalid)
            if (e.reason.find(sub) != std::string::npos) return true;
        return false;
    };
    assert(invalid_has("bad: invalid manifest"));
    assert(skipped_has(d, "broken.ttmod: invalid package"));
    assert(skipped_has(d, "g.mod: game not supported"));
    assert(skipped_has(d, "unsupported API"));
    // junk silent: no skipped lines for README/random.dll/other.zip/screenshots
    for (auto& s : d.skipped) {
        assert(s.find("README") == std::string::npos);
        assert(s.find("random.dll") == std::string::npos);
        assert(s.find("other.zip") == std::string::npos);
    }
    // duplicate: directory kept
    bool duppkg = true;
    assert(has_id(d, "dup.mod", &duppkg) && !duppkg);
    assert(skipped_has(d, "dup.mod: duplicate ID (kept directory)"));
    // sorted
    for (size_t i = 1; i < d.mods.size(); ++i) assert(d.mods[i - 1].id < d.mods[i].id);
    // disabled via state
    st.set(ttmod::ModId::parse("pkg.mod").value(), false);
    auto d2 = ttmod::discover_mods(kD, st, "minecraft-story-mode", 1);
    assert(!has_id(d2, "pkg.mod") && has_id(d2, "dev.mod"));
    assert(skipped_has(d2, "pkg.mod: disabled"));
    // ...but still visible for menus
    bool found_dis = false;
    for (auto& m : d2.disabled)
        if (m.id.str() == "pkg.mod") found_dis = true;
    assert(found_dis);
    // empty dir
    auto d3 = ttmod::discover_mods("/tmp/opencode_ttmod_discovery/nope", st, "minecraft-story-mode", 1);
    assert(d3.mods.empty());
    std::puts("discovery: all asserts passed");
    return 0;
}
