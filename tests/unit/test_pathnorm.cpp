#include "ttmod/pathnorm.hpp"
#include <cassert>
#include <cstdio>

using ttmod::normalize_win_path;
using ttmod::relative_key;
using ttmod::join_checked;

int main() {
    // Basics
    assert(normalize_win_path("C:\\Games\\MCSM\\x.LUA") == "c:/games/mcsm/x.lua");
    assert(normalize_win_path("c:/games//mcsm/./x.lua") == "c:/games/mcsm/x.lua");
    assert(normalize_win_path("c:/games/mcsm/sub/../x.lua") == "c:/games/mcsm/x.lua");
    // Extended prefixes
    assert(normalize_win_path("\\\\?\\H:\\A\\B.lua") == "h:/a/b.lua");
    assert(normalize_win_path("//?/H:/A/B.lua") == "h:/a/b.lua");
    assert(normalize_win_path("\\\\?\\UNC\\srv\\sh\\x") == "//srv/sh/x");
    // UNC preserved
    assert(normalize_win_path("\\\\srv\\sh\\x") == "//srv/sh/x");
    // Floor: .. above root dropped; drive: is an unpoppable floor
    assert(normalize_win_path("c:/../x") == "c:/x");
    assert(normalize_win_path("c:a/../x") == "c:/x");
    assert(normalize_win_path("//srv/sh/../x") == "//srv/sh/x");
    assert(normalize_win_path("a/../../x") == "../x");
    // Trailing slash
    assert(normalize_win_path("c:/games/") == "c:/games");
    assert(normalize_win_path("c:/") == "c:/");
    // relative_key
    auto k = relative_key("c:/g/mcsm/archives/x.lua", "c:/g/mcsm");
    assert(k && *k == "archives/x.lua");
    assert(relative_key("c:/g/mcsm", "c:/g/mcsm") && relative_key("c:/g/mcsm", "c:/g/mcsm")->empty());
    assert(!relative_key("c:/g/other/x", "c:/g/mcsm"));
    assert(!relative_key("c:/g/mcsm2/x", "c:/g/mcsm")); // prefix, not parent
    assert(!relative_key("d:/g/mcsm/x", "c:/g/mcsm"));
    // join_checked
    auto j = join_checked("c:/mods/a", "files/x.lua");
    assert(j && *j == "c:/mods/a/files/x.lua");
    assert(!join_checked("c:/mods/a", "../../win/x"));   // escape
    assert(!join_checked("c:/mods/a", "sub/../../.."));  // escape to parent
    assert(join_checked("c:/mods/a", "sub/../x") && *join_checked("c:/mods/a", "sub/../x") == "c:/mods/a/x");
    assert(!join_checked("c:/mods/a", ""));              // empty
    assert(!join_checked("c:/mods/a", "c:/other/x"));    // absolute smuggling
    // Trailing dots/spaces (Windows semantics)
    assert(normalize_win_path("c:/a/x. ") == "c:/a/x");
    assert(normalize_win_path("c:/a/x...") == "c:/a/x");
    assert(normalize_win_path("c:/a/.../x") == "c:/a/x"); // navigation kept
    std::puts("pathnorm: all asserts passed");
    return 0;
}
