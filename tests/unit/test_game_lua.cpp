// GameLuaRegistry: identity, role inference, lifecycle. The game owns every
// state; this only observes, so the tests use opaque handles.
#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/game_lua.hpp"

using namespace ttmod;

int main() {
    GameLuaRegistry reg;
    int a = 0x1000, b = 0x2000, c = 0x3000; // stand-in lua_State* handles

    // Capture order is 1-based and stable; observing twice does not duplicate.
    assert(reg.observe(&a) == 1);
    assert(reg.observe(&b) == 2);
    assert(reg.observe(&a) == 1); // idempotent
    assert(reg.count() == 2);
    assert(reg.contains(&a) && reg.contains(&b) && !reg.contains(&c));

    // Unknown until an identifying script is seen.
    assert(reg.role_of(&a) == LuaStateRole::Unknown);
    assert(reg.first(LuaStateRole::Engine) == nullptr);

    // Engine role from an engine script.
    assert(reg.note_script(&a, "h:/g/archives/_engine.lua"));
    assert(reg.role_of(&a) == LuaStateRole::Engine);
    assert(reg.first(LuaStateRole::Engine) == &a);
    // Identification is logged once; later scripts do not re-report.
    assert(!reg.note_script(&a, "h:/g/archives/Other.lua"));
    assert(reg.role_of(&a) == LuaStateRole::Engine);

    // Menu role, and Menu.lua wins even on a state that also loads engine
    // scripts (the more specific answer must stick).
    assert(reg.note_script(&b, "h:/g/archives/MCSM_pc_Menu_ms.ttarch2/Menu.lua"));
    assert(reg.role_of(&b) == LuaStateRole::Menu);
    assert(reg.note_script(&b, "_engine.lua") == false);
    assert(reg.role_of(&b) == LuaStateRole::Menu); // never relabelled

    // Unidentifying scripts count but do not name a role.
    assert(!reg.note_script(&c, "unseen.lua"));
    assert(reg.role_of(&c) == LuaStateRole::Unknown);

    // Inference rules, directly.
    assert(GameLuaRegistry::role_for_script("x/Menu.lua") == LuaStateRole::Menu);
    assert(GameLuaRegistry::role_for_script("x/_engine.lua") == LuaStateRole::Engine);
    assert(GameLuaRegistry::role_for_script("x/EngineTypes.lua") == LuaStateRole::Engine);
    assert(GameLuaRegistry::role_for_script("x/StoryBoardTracker.lua") == LuaStateRole::Engine);
    assert(GameLuaRegistry::role_for_script("x/random.lua") == LuaStateRole::Unknown);
    assert(GameLuaRegistry::role_for_script("") == LuaStateRole::Unknown);
    assert(GameLuaRegistry::role_for_script(nullptr) == LuaStateRole::Unknown);
    // Prefix must match the tail, not a substring.
    assert(GameLuaRegistry::role_for_script("Menu.lua.bak") == LuaStateRole::Unknown);

    // Script counts are tracked per state, including scripts that arrive after
    // the role was identified: _engine.lua, Other.lua, a1, a2.
    reg.note_script(&a, "a1.lua");
    reg.note_script(&a, "a2.lua");
    int seen_a = 0;
    for (auto& e : reg.entries())
        if (e.handle == &a) seen_a = e.scripts_seen;
    assert(seen_a == 4);

    // first() never returns an unidentified state for a concrete role.
    assert(reg.first(LuaStateRole::Other) == nullptr);

    // Shutdown clears observation; the game still owns the states.
    reg.clear();
    assert(reg.count() == 0 && !reg.contains(&a));
    assert(reg.first(LuaStateRole::Engine) == nullptr);

    // Role names are stable strings (logs depend on them).
    assert(std::string(to_string(LuaStateRole::Engine)) == "engine");
    assert(std::string(to_string(LuaStateRole::Menu)) == "menu");
    assert(std::string(to_string(LuaStateRole::Other)) == "other");
    assert(std::string(to_string(LuaStateRole::Unknown)) == "unknown");

    std::puts("game_lua: all asserts passed");
    return 0;
}