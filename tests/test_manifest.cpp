#include "ttmod/manifest.hpp"
#include "ttmod/moddeps.hpp"
#include <cassert>
#include <cstdio>

int main() {
    auto good = ttmod::parse_manifest(
        "{ \"id\": \"hello.mcsm\", \"version\": \"1.0.0\", \"api\": 1, "
        "\"games\": [\"minecraft-story-mode:s1\"], \"extra\": {\"a\":1} }");
    assert(good.ok && good.id == "hello.mcsm" && good.version == "1.0.0");
    assert(good.api == 1 && good.games.size() == 1 && good.games[0] == "minecraft-story-mode:s1");
    assert(good.priority == 100 && good.enabled && good.files.empty()); // defaults

    auto multi = ttmod::parse_manifest("{\"id\":\"x\",\"api\":2,\"games\":[\"a\",\"b\"]}");
    assert(multi.ok && multi.games.size() == 2 && multi.version.empty());

    auto res = ttmod::parse_manifest(
        "{\"id\":\"r.mod\",\"api\":1,\"priority\":200,\"enabled\":false,"
        "\"files\":{\"archives/x.lua\":\"files/archives/x.lua\",\"a/b\":\"c/d\"}}");
    assert(res.ok && res.priority == 200 && !res.enabled && res.files.size() == 2);
    assert(res.files[0].first == "archives/x.lua" && res.files[0].second == "files/archives/x.lua");

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
    assert(plug.ok && plug.plugin == "plugins/p.dll" && plug.arch == "x86" && plug.package_format == 1);
    auto plugdef = ttmod::parse_manifest("{\"id\":\"p\",\"api\":1}");
    assert(plugdef.ok && plugdef.plugin.empty() && plugdef.arch.empty() && plugdef.package_format == 1);

    auto dep = ttmod::parse_manifest(
        "{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"base\",\"version\":\"2.0\"},{\"id\":\"opt\"}],"
        "\"conflicts\":[\"rival\"]}");
    assert(dep.ok && dep.depends.size() == 2 && dep.conflicts.size() == 1);
    assert(dep.depends[0].first == "base" && dep.depends[0].second == "2.0");
    assert(dep.depends[1].first == "opt" && dep.depends[1].second.empty());
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"version\":\"1\"}]}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"conflicts\":\"x\"}").ok);

    assert(ttmod::compare_versions("1.0.0", "1.0.0") == 0);
    assert(ttmod::compare_versions("1.2", "1.2.0") == 0);
    assert(ttmod::compare_versions("2.0", "1.9.9") > 0);
    assert(ttmod::compare_versions("1.9", "1.10") < 0);
    assert(ttmod::compare_versions("1.0.0-beta", "1.0.0") == 0);
    assert(ttmod::compare_versions("", "0.0.1") < 0);

    // check_requirements over a present set
    ttmod::ModManifest base, addon, rival;
    base.id = "base";
    base.version = "2.1.0";
    addon.id = "addon";
    addon.depends = {{"base", "2.0"}, {"opt", ""}};
    rival.id = "rival";
    std::vector<ttmod::ModManifest> set1{base};
    assert(ttmod::check_requirements(addon, set1) == "missing dependency: opt");
    ttmod::ModManifest opt;
    opt.id = "opt";
    std::vector<ttmod::ModManifest> set2{base, opt, rival};
    assert(ttmod::check_requirements(addon, set2).empty());
    addon.conflicts = {"rival"};
    assert(ttmod::check_requirements(addon, set2) == "conflicts with present mod: rival");
    addon.conflicts.clear();
    addon.depends = {{"base", "3.0"}};
    assert(ttmod::check_requirements(addon, set2) == "dependency base version 2.1.0 < 3.0");
    std::puts("manifest: all asserts passed");
    return 0;
}
