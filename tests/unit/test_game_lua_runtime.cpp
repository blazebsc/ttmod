// GameLuaRuntime: authoritative game Lua state lifecycle.
// Tested without any game present, which is the point - the state lifecycle
// must be provable before it is wrapped around a real lua_State.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ttmod/game_lua_runtime.hpp"
#include "ttmod/modid.hpp"

using namespace ttmod;

static ModId mid(const char* s) {
    auto r = ModId::parse(s);
    assert(r.ok());
    return r.value();
}

int main() {
    // Basic lifecycle
    {
        GameLuaRuntime rt;
        assert(rt.count() == 0);
        assert(rt.entries().empty());
        int order = rt.observe((void*)0x1000);
        assert(order == 1);
        assert(rt.count() == 1);
        assert(rt.contains((void*)0x1000));
        assert(!rt.contains((void*)0x2000));
        order = rt.observe((void*)0x2000);
        assert(order == 2);
        assert(rt.count() == 2);
        rt.shutdown();
        assert(rt.count() == 0);
        assert(!rt.contains((void*)0x1000));
    }

    // Role inference via note_script
    {
        GameLuaRuntime rt;
        rt.observe((void*)0x1000);
        assert(rt.role_of((void*)0x1000) == LuaStateRole::Unknown);
        bool identified = rt.note_script((void*)0x1000, "scripts/_engine.lua");
        assert(identified);
        assert(rt.role_of((void*)0x1000) == LuaStateRole::Engine);
        assert(rt.first(LuaStateRole::Engine) == (void*)0x1000);
        assert(rt.first(LuaStateRole::Menu) == nullptr);

        rt.observe((void*)0x2000);
        identified = rt.note_script((void*)0x2000, "scripts/Menu.lua");
        assert(identified);
        assert(rt.role_of((void*)0x2000) == LuaStateRole::Menu);
        assert(rt.first(LuaStateRole::Menu) == (void*)0x2000);

        // Unknown script doesn't change role
        rt.observe((void*)0x3000);
        identified = rt.note_script((void*)0x3000, "scripts/random.lua");
        assert(!identified);
        assert(rt.role_of((void*)0x3000) == LuaStateRole::Unknown);
    }

    // Multiple scripts on same state - first one wins
    {
        GameLuaRuntime rt;
        rt.observe((void*)0x1000);
        rt.note_script((void*)0x1000, "scripts/Menu.lua");
        assert(rt.role_of((void*)0x1000) == LuaStateRole::Menu);
        rt.note_script((void*)0x1000, "scripts/_engine.lua");
        assert(rt.role_of((void*)0x1000) == LuaStateRole::Menu); // first wins
    }

    // Observe-only mode
    {
        GameLuaRuntimeOptions opt;
        opt.observe_only = true;
        GameLuaRuntime rt(opt);
        assert(rt.observe_only());
        rt.observe((void*)0x1000);
        // Game-thread operations should fail in observe-only
        auto r1 = rt.run_chunk_on_game_thread((void*)0x1000, "return 1", "test.lua");
        assert(!r1.ok());
        assert(r1.error().category == errcat::kIO);
        auto r2 = rt.get_global_on_game_thread((void*)0x1000, "foo");
        assert(!r2.ok());
        auto r3 = rt.set_global_on_game_thread((void*)0x1000, "foo", Value::number(1));
        assert(!r3.ok());
    }

    // Allowed mods coordination
    {
        GameLuaRuntime rt;
        std::vector<ModId> allowed = {mid("mod.a"), mid("mod.b")};
        rt.set_allowed_mods(allowed);
        // No direct API to query yet; this is for the Windows layer to enforce
    }

    // Invalid operations
    {
        GameLuaRuntime rt;
        assert(rt.observe(nullptr) == -1);
        assert(!rt.note_script(nullptr, "test.lua"));
        assert(!rt.note_script((void*)0x1000, nullptr));
        assert(rt.role_of(nullptr) == LuaStateRole::Unknown);
        assert(rt.first(LuaStateRole::Engine) == nullptr);
        auto r = rt.run_chunk_on_game_thread(nullptr, "x", "x");
        assert(!r.ok());
        r = rt.run_chunk_on_game_thread((void*)0x1000, "x", "x");
        assert(!r.ok()); // unknown handle
    }

    // entries() returns copies safe for iteration
    {
        GameLuaRuntime rt;
        rt.observe((void*)0x1000);
        rt.note_script((void*)0x1000, "scripts/Menu.lua");
        rt.observe((void*)0x2000);
        auto e = rt.entries();
        assert(e.size() == 2);
        assert(e[0].handle == (void*)0x1000);
        assert(e[1].handle == (void*)0x2000);
    }

    // Shutdown is idempotent
    {
        GameLuaRuntime rt;
        rt.observe((void*)0x1000);
        rt.shutdown();
        assert(rt.count() == 0);
        rt.shutdown(); // second call OK
        assert(rt.count() == 0);
    }

    std::puts("game_lua_runtime: all asserts passed");
    return 0;
}