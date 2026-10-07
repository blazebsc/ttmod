#include "ttmod/resolver.hpp"
#include <cassert>
#include <cstdio>

using ttmod::ModDef;
using ttmod::ModId;
using ttmod::Resolver;

static ModId mid(const char* id) {
    return ModId::parse(id).value();
}

static bool always_exists(const std::string&) { return true; }
static bool nothing_exists(const std::string&) { return false; }

int main() {
    Resolver r;
    // No root: miss with reason
    auto q0 = r.resolve("c:/g/x");
    assert(!q0.found && q0.reason == "no-root");

    r.set_game_root("C:\\Games\\MCSM");
    // Scope: outside root never eligible
    assert(r.resolve("c:\\windows\\x.dll").reason == "outside-root");
    assert(r.resolve("\\\\?\\C:\\Windows\\x").reason == "outside-root");
    // Miss
    assert(r.resolve("c:/games/mcsm/archives/x.lua").reason == "miss");

    // One mod, mixed separators/case in request
    ModDef a{mid("mod.a"), "c:/mods/a", 100, true, {{"archives/x.lua", "files/archives/x.lua"}}};
    assert(r.add_mod(a, always_exists));
    auto h1 = r.resolve("\\\\?\\C:\\GAMES\\mcsm\\ARCHIVES\\x.lua");
    assert(h1.found && h1.winner == mid("mod.a") && h1.priority == 100);
    assert(h1.replacement == "c:/mods/a/files/archives/x.lua" && h1.shadowed.empty());

    // Priority: higher wins, conflict recorded; tie -> smallest id
    ModDef b{mid("mod.b"), "c:/mods/b", 200, true, {{"archives/x.lua", "f/x.lua"}}};
    ModDef c{mid("mod.c"), "c:/mods/c", 200, true, {{"archives/x.lua", "g/x.lua"}}};
    assert(r.add_mod(b, always_exists));
    assert(r.add_mod(c, always_exists));
    auto h2 = r.resolve("c:/games/mcsm/archives/x.lua");
    assert(h2.found && h2.winner == mid("mod.b") && h2.priority == 200);
    assert(h2.shadowed.size() == 2 && h2.shadowed[0] == mid("mod.c") && h2.shadowed[1] == mid("mod.a"));

    // Disabled mod invisible
    ModDef d{mid("mod.d"), "c:/mods/d", 999, false, {{"archives/x.lua", "f/x.lua"}}};
    assert(!r.add_mod(d, always_exists));
    assert(r.resolve("c:/games/mcsm/archives/x.lua").winner == mid("mod.b"));

    // Invalid overrides fall back safely with problems recorded
    Resolver r2;
    r2.set_game_root("c:/g");
    ModDef bad{
        mid("bad"), "c:/mods/bad", 100, true, {{"x", "../escape"}, {"y", "missing.lua"}, {"c:/other/z", "f.lua"}}};
    assert(!r2.add_mod(bad, nothing_exists)); // nothing usable
    assert(r2.problems().size() == 3);
    assert(r2.resolve("c:/g/x").reason == "miss");
    // escape rejected even when file "exists"
    Resolver r3;
    r3.set_game_root("c:/g");
    ModDef esc{mid("esc"), "c:/mods/esc", 100, true, {{"x", "../escape"}}};
    assert(!r3.add_mod(esc, always_exists));
    assert(r3.resolve("c:/g/x").reason == "miss");

    Resolver r4;
    r4.set_game_root("c:/g");
    ModDef invalid{ModId{}, "c:/mods/invalid", 100, true, {{"x", "f.lua"}}};
    assert(!r4.add_mod(invalid, always_exists));
    assert(r4.problems().size() == 1 && r4.problems()[0] == "mod rejected: empty id");

    std::puts("resolver: all asserts passed");
    return 0;
}
