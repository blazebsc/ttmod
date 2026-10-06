#include "ttmod/manifest.hpp"
#include "ttmod/moddeps.hpp"
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

    // check_requirements over a present set
    ttmod::ModManifest base, addon, rival;
    base.identity.id = "base";
    base.identity.version = "2.1.0";
    addon.identity.id = "addon";
    addon.deps.depends = {{"base", "2.0"}, {"opt", ""}};
    rival.identity.id = "rival";
    std::vector<ttmod::ModManifest> set1{base};
    assert(ttmod::check_requirements(addon, set1) == "missing dependency: opt");
    ttmod::ModManifest opt;
    opt.identity.id = "opt";
    std::vector<ttmod::ModManifest> set2{base, opt, rival};
    assert(ttmod::check_requirements(addon, set2).empty());
    addon.deps.conflicts = {"rival"};
    assert(ttmod::check_requirements(addon, set2) == "conflicts with present mod: rival");
    addon.deps.conflicts.clear();
    addon.deps.depends = {{"base", "3.0"}};
    assert(ttmod::check_requirements(addon, set2) == "dependency base version 2.1.0 < 3.0");
    std::puts("manifest: all asserts passed");
    return 0;
}
