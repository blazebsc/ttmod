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
    assert(k.ok() && k.value() == "archives/x.lua");
    auto root_key = relative_key("c:/g/mcsm", "c:/g/mcsm");
    assert(root_key.ok() && root_key.value().empty());
    auto assert_traversal = [](const ttmod::Result<std::string>& result, const char* operation, const char* object) {
        assert(!result.ok());
        assert(result.error().category == ttmod::errcat::kTraversal);
        assert(result.error().operation == operation);
        assert(result.error().object == object);
    };
    auto outside = relative_key("c:/g/other/x", "c:/g/mcsm");
    assert_traversal(outside, "relative-key", "c:/g/other/x");
    auto prefix = relative_key("c:/g/mcsm2/x", "c:/g/mcsm");
    assert_traversal(prefix, "relative-key", "c:/g/mcsm2/x");
    auto other_drive = relative_key("d:/g/mcsm/x", "c:/g/mcsm");
    assert_traversal(other_drive, "relative-key", "d:/g/mcsm/x");
    // join_checked
    auto j = join_checked("c:/mods/a", "files/x.lua");
    assert(j.ok() && j.value() == "c:/mods/a/files/x.lua");
    auto escaped = join_checked("c:/mods/a", "../../win/x");
    assert_traversal(escaped, "join-path", "../../win/x");
    auto escaped_parent = join_checked("c:/mods/a", "sub/../../..");
    assert_traversal(escaped_parent, "join-path", "sub/../../..");
    auto normalized = join_checked("c:/mods/a", "sub/../x");
    assert(normalized.ok() && normalized.value() == "c:/mods/a/x");
    auto empty = join_checked("c:/mods/a", "");
    assert_traversal(empty, "join-path", "");
    auto absolute = join_checked("c:/mods/a", "c:/other/x");
    assert_traversal(absolute, "join-path", "c:/other/x");
    // Trailing dots/spaces (Windows semantics)
    assert(normalize_win_path("c:/a/x. ") == "c:/a/x");
    assert(normalize_win_path("c:/a/x...") == "c:/a/x");
    assert(normalize_win_path("c:/a/.../x") == "c:/a/x"); // navigation kept
    std::puts("pathnorm: all asserts passed");
    return 0;
}
