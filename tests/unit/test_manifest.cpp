#include "ttmod/manifest.hpp"
#include "ttmod/modgraph.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/version.hpp"
#include <cassert>
#include <cstdio>
#include <utility>

// Test helper: every id here is a literal we control, so parse must succeed.
static ttmod::ModId mid(const char* s) {
    auto r = ttmod::ModId::parse(s);
    assert(r.ok());
    return r.value();
}

static ttmod::VersionConstraint con(const char* s) {
    auto r = ttmod::VersionConstraint::parse(s);
    assert(r.ok());
    return std::move(r).value();
}

static std::string manifest_with_games(const std::vector<std::string>& games) {
    std::string text = "{\"id\":\"g\",\"api\":1,\"games\":[";
    for (size_t i = 0; i < games.size(); ++i) {
        if (i) text += ",";
        text += "\"" + games[i] + "\"";
    }
    return text + "]}";
}

static void expect_manifest_error(const std::string& text, const char* category, const std::string& message_part) {
    auto r = ttmod::parse_manifest(text);
    assert(!r.ok());
    assert(r.error().category == category);
    assert(r.error().message.find(message_part) != std::string::npos);
}

int main() {
    auto good = ttmod::parse_manifest("{ \"id\": \"hello.mcsm\", \"version\": \"1.0.0\", \"api\": 1, "
                                      "\"games\": [\"minecraft-story-mode:s1\"], \"extra\": {\"a\":1} }")
                    .value();
    assert(good.identity.id == mid("hello.mcsm") && good.identity.version.str() == "1.0.0");
    assert(good.compat.api == 1 && good.compat.games.size() == 1 && good.compat.games[0] == "minecraft-story-mode:s1");
    assert(good.overrides.priority == 100 && good.enabled && good.overrides.files.empty()); // defaults

    auto multi = ttmod::parse_manifest("{\"id\":\"x\",\"api\":2,\"games\":[\"a\",\"b\"]}").value();
    assert(multi.compat.games.size() == 2 && multi.identity.version.str().empty());

    auto res = ttmod::parse_manifest("{\"id\":\"r.mod\",\"api\":1,\"priority\":200,\"enabled\":false,"
                                     "\"files\":{\"archives/x.lua\":\"files/archives/x.lua\",\"a/b\":\"c/d\"}}")
                   .value();
    assert(res.overrides.priority == 200 && !res.enabled && res.overrides.files.size() == 2);
    assert(res.overrides.files[0].first == "archives/x.lua" && res.overrides.files[0].second == "files/archives/x.lua");

    auto normalized = ttmod::parse_manifest("{\"id\":\"paths\",\"api\":1,\"plugin\":\"./plugins/x.dll\","
                                            "\"files\":{\"a/x.lua\":\"files\\\\sub\\\\..\\\\x.lua\"}}")
                          .value();
    assert(normalized.overrides.files[0].first == "a/x.lua" && normalized.overrides.files[0].second == "files/x.lua");
    assert(normalized.plugin.path == "plugins/x.dll");

    assert(!ttmod::parse_manifest("").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\"}").ok());               // missing api
    assert(!ttmod::parse_manifest("{\"api\":1}").ok());                  // missing id
    assert(!ttmod::parse_manifest("{\"id\":1,\"api\":1}").ok());         // id not string
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":\"1\"}").ok()); // api not int
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"priority\":\"hi\"}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"enabled\":\"yes\"}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"files\":[]}").ok()); // files not object
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"files\":{\"a\":1}}").ok());

    auto valid_games =
        ttmod::parse_manifest(manifest_with_games({"a", "minecraft-story-mode:s1", "x:s123", "x:s10"})).value();
    assert(valid_games.compat.games.size() == 4);
    assert(valid_games.compat.games[0] == "a" && valid_games.compat.games[1] == "minecraft-story-mode:s1" &&
           valid_games.compat.games[2] == "x:s123" && valid_games.compat.games[3] == "x:s10");
    for (const char* bad : {"", "Minecraft", "a:s", "a:sx", "a:s1234", "a b", ":s1", "a:s0", "a:s01", "a:s001"}) {
        auto failed = ttmod::parse_manifest(manifest_with_games({bad}));
        assert(!failed.ok() && failed.error().category == ttmod::errcat::kType);
        assert(failed.error().message == std::string("bad games: ") + bad);
    }
    const std::string long_game(65, 'a');
    auto long_game_result = ttmod::parse_manifest(manifest_with_games({long_game}));
    assert(!long_game_result.ok() && long_game_result.error().category == ttmod::errcat::kType);
    assert(long_game_result.error().message == "bad games: " + long_game);
    expect_manifest_error(manifest_with_games({"a", "a"}), ttmod::errcat::kDuplicate, "a");

    {
        ttmod::ModCompatibility compat;
        compat.api = 0;
        assert(!compat.supports_api(3));
        compat.api = 1;
        assert(compat.supports_api(3));
        compat.api = 3;
        assert(compat.supports_api(3));
        compat.api = 4;
        assert(!compat.supports_api(3));

        compat.games = {"g:s1", "bare"};
        assert(compat.supports_game("g", 1));
        assert(compat.supports_game("bare", 2));
        assert(!compat.supports_game("g", 2));
        ttmod::ModCompatibility no_games;
        assert(!no_games.supports_game("g", 1));

        compat.arch = ttmod::Architecture::Any;
        assert(compat.supports_arch(ttmod::Architecture::X86));
        assert(compat.supports_arch(ttmod::Architecture::X64));
        compat.arch = ttmod::Architecture::X64;
        assert(!compat.supports_arch(ttmod::Architecture::X86));
        assert(compat.supports_arch(ttmod::Architecture::X64));
    }

    auto plug = ttmod::parse_manifest(
                    "{\"id\":\"p\",\"api\":1,\"plugin\":\"plugins/p.dll\",\"arch\":\"x86\",\"package_format\":1}")
                    .value();
    assert(plug.plugin.path == "plugins/p.dll" && plug.compat.arch == ttmod::Architecture::X86 &&
           plug.package_format == 1);
    auto plugdef = ttmod::parse_manifest("{\"id\":\"p\",\"api\":1}").value();
    assert(plugdef.plugin.path.empty() && plugdef.compat.arch == ttmod::Architecture::Any &&
           plugdef.package_format == 1);

    auto dep = ttmod::parse_manifest(
                   "{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"base\",\"version\":\"2.0\"},{\"id\":\"opt\"}],"
                   "\"conflicts\":[\"rival\"]}")
                   .value();
    assert(dep.deps.depends.size() == 2 && dep.deps.conflicts.size() == 1);
    assert(dep.deps.depends[0].first == mid("base") && dep.deps.depends[0].second.str() == "2.0");
    assert(dep.deps.depends[1].first == mid("opt") && dep.deps.depends[1].second.empty());
    assert(dep.deps.conflicts[0] == mid("rival"));
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"version\":\"1\"}]}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"conflicts\":\"x\"}").ok());
    // Dependency and conflict ids go through the same canonical validator.
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"../evil\"}]}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"conflicts\":[\"..\\\\evil\"]}").ok());
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"m\"}]}", ttmod::errcat::kType, "m");
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"conflicts\":[\"m\"]}", ttmod::errcat::kType, "m");
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"base\"},{\"id\":\"base\"}]}",
                          ttmod::errcat::kDuplicate, "base");
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"conflicts\":[\"base\",\"base\"]}", ttmod::errcat::kDuplicate,
                          "base");
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"base\"}],\"conflicts\":[\"base\"]}",
                          ttmod::errcat::kType, "base");
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"runtimes\":[\"lua\",\"lua\"]}", ttmod::errcat::kDuplicate, "lua");
    expect_manifest_error("{\"id\":\"m\",\"api\":1,\"permissions\":[\"game.read\",\"game.read\"]}",
                          ttmod::errcat::kDuplicate, "game.read");

    // Version + VersionConstraint are parsed, never constructed raw.
    {
        auto v = [](const char* s) { return ttmod::Version::parse(s).value(); };
        auto c = [](const char* s) { return ttmod::VersionConstraint::parse(s).value(); };
        assert(v("1.0.0").compare(v("1.0.0")) == 0);
        assert(v("1.2").compare(v("1.2.0")) == 0);
        assert(v("2.0").compare(v("1.9.9")) > 0);
        assert(v("1.9").compare(v("1.10")) < 0);
        assert(v("1.0.0-beta").compare(v("1.0.0")) == 0);
        assert(v("").compare(v("0.0.1")) < 0);
        assert(c("").satisfied_by(v("9.9")));
        assert(c("2.0").satisfied_by(v("2.1.0")));
        assert(!c("3.0").satisfied_by(v("2.1.0")));
        assert(c("=2.1.0").satisfied_by(v("2.1.0")));
        assert(!c("=2.1.0").satisfied_by(v("2.1.1")));
        assert(c("<3.0").satisfied_by(v("2.9.9")));
        assert(c(">2.0").satisfied_by(v("2.0.1")));
    }

    // Malformed and overflowing versions fail the manifest (bounded policy).
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"version\":\"1.x\"}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"version\":\"9999999999\"}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"version\":\"1.0.0.0.0\"}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"version\":1}").ok());
    // A malformed dependency constraint also fails the manifest.
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"b\",\"version\":\">=x.y\"}]}").ok());
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"b\",\"version\":\">=\"}]}").ok());

    // Dependency graph over a present set (replaces per-mod string checks).
    auto mk = [](const char* id, const char* ver) {
        ttmod::ModManifest m;
        m.identity.id = mid(id);
        m.identity.version = ttmod::Version::parse(ver).value();
        return m;
    };
    {
        ttmod::ModManifest base = mk("base", "2.1.0"), addon = mk("addon", "1.0"), opt = mk("opt", "1.0"),
                           rival = mk("rival", "1.0");
        addon.deps.depends = {{mid("base"), con("2.0")}, {mid("opt"), con("")}};
        auto r = ttmod::resolve_dependencies({base, addon, opt, rival});
        assert(r.blocked(mid("addon")) == false);
        assert(r.load_order.front() == mid("base")); // deps first
    }
    {
        // missing dependency blocks the dependent only
        ttmod::ModManifest a = mk("a", "1.0");
        a.deps.depends = {{mid("ghost"), con("")}};
        auto r = ttmod::resolve_dependencies({a, mk("b", "1.0")});
        assert(r.blocked(mid("a")) && !r.blocked(mid("b")));
        bool found = false;
        for (auto& p : r.problems)
            if (p.mod == mid("a") && p.category == "missing") found = true;
        assert(found);
    }
    {
        // version mismatch + conflict + cycle + duplicate
        ttmod::ModManifest a = mk("a", "2.1.0"), b = mk("b", "2.1.0"), c = mk("c", "1.0");
        b.deps.depends = {{mid("a"), con("<2.0")}};
        auto r1 = ttmod::resolve_dependencies({a, b});
        assert(r1.blocked(mid("b")));
        bool ver = false;
        for (auto& p : r1.problems)
            if (p.mod == mid("b") && p.category == "version") {
                ver = true;
                assert(p.message.find("does not satisfy <2.0") != std::string::npos);
            }
        assert(ver);
        c.deps.conflicts = {mid("a")};
        auto r2 = ttmod::resolve_dependencies({a, c});
        assert(r2.blocked(mid("c")));
        ttmod::ModManifest x = mk("x", "1.0"), y = mk("y", "1.0");
        x.deps.depends = {{mid("y"), con("")}};
        y.deps.depends = {{mid("x"), con("")}};
        auto r3 = ttmod::resolve_dependencies({x, y});
        assert(r3.blocked(mid("x")) && r3.blocked(mid("y")));
        auto r4 = ttmod::resolve_dependencies({mk("d", "1.0"), mk("d", "2.0")});
        assert(r4.blocked(mid("d")));
        // duplicates keep going exactly once in load order
        int dcount = 0;
        for (auto& id : r4.load_order)
            if (id == mid("d")) ++dcount;
        assert(dcount == 1);
    }
    std::puts("manifest: all asserts passed");
    return 0;
}
