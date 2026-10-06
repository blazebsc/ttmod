#include "ttmod/validate.hpp"
#include "ttmod/manifest.hpp"
#include <cassert>
#include <cstdio>

using ttmod::is_valid_mod_id;
using ttmod::validate_mod_relative_path;

int main() {
    // Mod IDs: 1-64 chars, [A-Za-z0-9_.-], no traversal, no space padding.
    assert(is_valid_mod_id("hello.mcsm"));
    assert(is_valid_mod_id("a"));
    assert(is_valid_mod_id("A-_0.9"));
    assert(is_valid_mod_id(std::string(64, 'a')));
    assert(!is_valid_mod_id(""));
    assert(!is_valid_mod_id(std::string(65, 'a')));
    assert(!is_valid_mod_id("."));
    assert(!is_valid_mod_id(".."));
    assert(!is_valid_mod_id("a/b"));
    assert(!is_valid_mod_id("a\\b"));
    assert(!is_valid_mod_id("a:b"));
    assert(!is_valid_mod_id("a..b"));
    assert(!is_valid_mod_id(" a"));
    assert(!is_valid_mod_id("a "));
    assert(!is_valid_mod_id("a b"));
    assert(!is_valid_mod_id("é"));
    // Relative paths: normalized forward slashes, no escape from mod root.
    auto ok = [](const char* p, const char* want) {
        auto r = validate_mod_relative_path(p);
        assert(r.ok() && r.value() == want);
    };
    auto bad = [](const char* p, const char* cat) {
        auto r = validate_mod_relative_path(p);
        assert(!r.ok() && r.error().category == cat);
    };
    ok("files/x.lua", "files/x.lua");
    ok("files\\x.lua", "files/x.lua");
    ok("./x.lua", "x.lua");
    ok("a/./b/../c", "a/c");
    bad("", "missing");
    bad("/abs/x", "absolute");
    bad("C:/evil", "absolute");
    bad("C:\\evil", "absolute");
    bad("//server/share", "absolute");
    bad("../../evil", "traversal");
    bad("a/../../evil", "traversal");
    bad("..\\..\\evil", "traversal");
    bad("..", "traversal");
    bad(".", "missing");
    {
        std::string deep;
        for (int i = 0; i < 40; ++i) deep += "d" + std::to_string(i) + "/";
        deep += "x";
        bad(deep.c_str(), "limit"); // >32 components
    }
    bad("~/evil", "absolute");
    bad("a/b:c", "absolute");
    // Manifest-level: duplicates and trailing garbage rejected.
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"id\":\"y\",\"api\":1}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1} trailing").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"../evil\",\"api\":1}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1.5}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":99999999999999999999}").ok);
    // Malformed inputs: syntax, truncation, lone surrogates, non-finite.
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,}").ok); // trailing comma
    assert(!ttmod::parse_manifest("{\"id\":\"x\" \"api\":1}").ok); // missing colon
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1").ok); // truncation
    assert(!ttmod::parse_manifest("[{\"id\":\"x\",\"api\":1}]").ok); // top-level array
    assert(!ttmod::parse_manifest("null").ok);
    assert(!ttmod::parse_manifest(std::string("{\"id\":\"") + "\\ud800" + "\",\"api\":1}").ok); // lone surrogate
    assert(ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"extra\":{\"deep\":[1,{\"k\":null}]}}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":1,\"config\":[{\"key\":\"k\",\"type\":\"int\",\"label\":\"L\",\"default\":1e400}]}").ok);
    assert(!ttmod::parse_manifest("{\"id\":\"x\",\"api\":-1}").ok);
    // Runtime declarations (doc §§19-21): parsed, validated, surfaced.
    {
        auto mr = ttmod::parse_manifest("{\"id\":\"u\",\"api\":1,\"runtimes\":[\"native\",\"luau\",\"telltale-lua\"],"
                                        "\"permissions\":[\"game.read\",\"hooks\"]}");
        assert(mr.ok && mr.runtime.runtimes.size() == 3 && mr.runtime.permissions.size() == 2);
        assert(!ttmod::parse_manifest("{\"id\":\"u\",\"api\":1,\"runtimes\":[\"ps5\"]}").ok);
        assert(!ttmod::parse_manifest("{\"id\":\"u\",\"api\":1,\"runtimes\":\"native\"}").ok);
        assert(!ttmod::parse_manifest("{\"id\":\"u\",\"api\":1,\"permissions\":[\"sudo\"]}").ok);
        assert(ttmod::parse_manifest("{\"id\":\"u\",\"api\":1}").ok &&
               ttmod::parse_manifest("{\"id\":\"u\",\"api\":1}").runtime.runtimes.empty());
    }
    printf("security: validators OK\n");
    return 0;
}
