#include "ttmod/manifest.hpp"
#include "ttmod/modgraph.hpp"
#include "ttmod/version.hpp"
#include <cassert>
#include <cstdio>

int main() {
    auto good = ttmod::parse_manifest(
        "{ \"id\": \"hello.mcsm\", \"version\": \"1.0.0\", \"api\": 1, "
        "\"games\": [\"minecraft-story-mode:s1\"], \"extra\": {\"a\":1} }");
    assert(good.ok && good.identity.id == "hello.mcsm" && good.identity.version == "1.0.0");
    assert(good.compat.api == 1 && good.compat.games.size() == 1 && good.compat.games[0] == "minecraft-story-mode:s1");
    assert(good.overrides.priority == 100 && good.enabled && good.overrides.files.empty()); // defaults

    auto multi = ttmod::parse_manifest("{\"id\":\"x\",\"api\":2,\"games\":[\"a\",\"b\"]}");
    assert(multi.ok && multi.compat.games.size() == 2 && multi.identity.version.empty());

    auto res = ttmod::parse_manifest(
        "{\"id\":\"r.mod\",\"api\":1,\"priority\":200,\"enabled\":false,"
        "\"files\":{\"archives/x.lua\":\"files/archives/x.lua\",\"a/b\":\"c/d\"}}");
    assert(res.ok && res.overrides.priority == 200 && !res.enabled && res.overrides.files.size() == 2);
    assert(res.overrides.files[0].first == "archives/x.lua" && res.overrides.files[0].second == "files/archives/x.lua");

    assert(!ttmod::parse_manifest("").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\"}").ok);            // missing api
    assert(!ttmod::parse_manifest("{\"api\":1}").ok);               // missing id
    assert(!ttmod::parse_manifest("{\"id\":1,\"api\":1}").ok);      // id not string
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":\"1\"}").ok); // api not int
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"priority\":\"hi\"}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"enabled\":\"yes\"}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"files\":[]}").ok); // files not object
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"files\":{\"a\":1}}").ok);

    auto plug = ttmod::parse_manifest(
        "{\"id\":\"p\",\"api\":1,\"plugin\":\"plugins/p.dll\",\"arch\":\"x86\",\"package_format\":1}");
    assert(plug.ok && plug.plugin.path == "plugins/p.dll" && plug.compat.arch == ttmod::Architecture::X86 && plug.package_format == 1);
    auto plugdef = ttmod::parse_manifest("{\"id\":\"p\",\"api\":1}");
    assert(plugdef.ok && plugdef.plugin.path.empty() && plugdef.compat.arch == ttmod::Architecture::Any && plugdef.package_format == 1);

    auto dep = ttmod::parse_manifest(
        "{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"base\",\"version\":\"2.0\"},{\"id\":\"opt\"}],"
        "\"conflicts\":[\"rival\"]}");
    assert(dep.ok && dep.deps.depends.size() == 2 && dep.deps.conflicts.size() == 1);
    assert(dep.deps.depends[0].first == "base" && dep.deps.depends[0].second == "2.0");
    assert(dep.deps.depends[1].first == "opt" && dep.deps.depends[1].second.empty());
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"version\":\"1\"}]}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"conflicts\":\"x\"}").ok);

    assert(ttmod::compare_versions("1.0.0", "1.0.0") == 0);
    assert(ttmod::compare_versions("1.2", "1.2.0") == 0);
    assert(ttmod::compare_versions("2.0", "1.9.9") > 0);
    assert(ttmod::compare_versions("1.9", "1.10") < 0);
    assert(ttmod::compare_versions("1.0.0-beta", "1.0.0") == 0);
    assert(ttmod::compare_versions("", "0.0.1") < 0);

    // Version + VersionConstraint (Stage C domain model)
    assert(ttmod::Version("1.2.0").compare(ttmod::Version("1.2")) == 0);
    assert(ttmod::Version("1.10").compare(ttmod::Version("1.9")) > 0);
    assert(ttmod::VersionConstraint("").satisfied_by(ttmod::Version("9.9")));
    assert(ttmod::VersionConstraint("2.0").satisfied_by(ttmod::Version("2.1.0")));
    assert(!ttmod::VersionConstraint("3.0").satisfied_by(ttmod::Version("2.1.0")));
    assert(ttmod::VersionConstraint("=2.1.0").satisfied_by(ttmod::Version("2.1.0")));
    assert(!ttmod::VersionConstraint("=2.1.0").satisfied_by(ttmod::Version("2.1.1")));
    assert(ttmod::VersionConstraint("<3.0").satisfied_by(ttmod::Version("2.9.9")));
    assert(ttmod::VersionConstraint(">2.0").satisfied_by(ttmod::Version("2.0.1")));

    // Dependency graph over a present set (replaces per-mod string checks).
    auto mk = [](const char* id, const char* ver) {
        ttmod::ModManifest m;
        m.identity.id = id;
        m.identity.version = ver;
        return m;
    };
    {
        ttmod::ModManifest base = mk("base", "2.1.0"), addon = mk("addon", "1.0"), opt = mk("opt", "1.0"),
                           rival = mk("rival", "1.0");
        addon.deps.depends = {{"base", "2.0"}, {"opt", ""}};
        auto r = ttmod::resolve_dependencies({base, addon, opt, rival});
        assert(r.blocked("addon") == false);
        assert(r.load_order.front() == "base"); // deps first
    }
    {
        // missing dependency blocks the dependent only
        ttmod::ModManifest a = mk("a", "1.0");
        a.deps.depends = {{"ghost", ""}};
        auto r = ttmod::resolve_dependencies({a, mk("b", "1.0")});
        assert(r.blocked("a") && !r.blocked("b"));
        bool found = false;
        for (auto& p : r.problems)
            if (p.mod == "a" && p.category == "missing") found = true;
        assert(found);
    }
    {
        // version mismatch + conflict + cycle + duplicate
        ttmod::ModManifest a = mk("a", "1.0"), b = mk("b", "2.1.0"), c = mk("c", "1.0");
        b.deps.depends = {{"a", "3.0"}};
        auto r1 = ttmod::resolve_dependencies({a, b});
        assert(r1.blocked("b"));
        bool ver = false;
        for (auto& p : r1.problems)
            if (p.mod == "b" && p.category == "version") ver = true;
        assert(ver);
        c.deps.conflicts = {"a"};
        auto r2 = ttmod::resolve_dependencies({a, c});
        assert(r2.blocked("c"));
        ttmod::ModManifest x = mk("x", "1.0"), y = mk("y", "1.0");
        x.deps.depends = {{"y", ""}};
        y.deps.depends = {{"x", ""}};
        auto r3 = ttmod::resolve_dependencies({x, y});
        assert(r3.blocked("x") && r3.blocked("y"));
        auto r4 = ttmod::resolve_dependencies({mk("d", "1.0"), mk("d", "2.0")});
        assert(r4.blocked("d"));
        // duplicates keep going exactly once in load order
        int dcount = 0;
        for (auto& id : r4.load_order)
            if (id == "d") ++dcount;
        assert(dcount == 1);
    }
    std::puts("manifest: all asserts passed");
    return 0;
}
