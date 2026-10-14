#include <cstddef>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>

#include "ttmod/plugin_api.h"
#include "ttmod/plugin_grant.hpp"
#include "ttmod/version.hpp"

int main() {
    using ttmod::Permission;
    using ttmod::Runtime;
    // Version mirror pin (AGENTS.md: kVersion + kApiVersion move together
    // with project() and TTMOD_PLUGIN_API_VERSION). If the ABI revs, this
    // fails until all three move - that is the point.
    assert(ttmod::kApiVersion == TTMOD_PLUGIN_API_VERSION);
    assert(TTMOD_PLUGIN_API_VERSION == 6);
    // Layout, append-only order: api_version first, negotiation pair last.
    // Relative order only - absolute offsets differ between Win32 x86 (the
    // ABI target, 4-byte pointers) and the LP64 test host, so no exact
    // byte values are pinned here. Reordering or inserting above
    // struct_size breaks old plugins and must fail this test.
    assert(offsetof(ttmod_host, api_version) == 0);
    const size_t offs[] = {
        offsetof(ttmod_host, api_version), offsetof(ttmod_host, profile_id),
        offsetof(ttmod_host, game), offsetof(ttmod_host, season),
        offsetof(ttmod_host, log), offsetof(ttmod_host, subscribe),
        offsetof(ttmod_host, unsubscribe), offsetof(ttmod_host, get_state),
        offsetof(ttmod_host, get_mod_count), offsetof(ttmod_host, get_mod_info),
        offsetof(ttmod_host, queue_ui_chunk), offsetof(ttmod_host, struct_size),
        offsetof(ttmod_host, reserved), offsetof(ttmod_host, register_api),
    };
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); ++i) {
        if (i == 0)
            assert(offs[i] == 0);
        else
            assert(offs[i] > offs[i - 1]);
    }
    // v6 value struct order (plain C across the boundary).
    assert(offsetof(ttmod_value, type) == 0);
    assert(offsetof(ttmod_value, b) > offsetof(ttmod_value, type));
    assert(offsetof(ttmod_value, n) > offsetof(ttmod_value, b));
    assert(offsetof(ttmod_value, s) > offsetof(ttmod_value, n));
    // Fixed buffers are platform-independent: pin them exactly.
    assert(sizeof(((ttmod_state*)nullptr)->save_dir) == 256);
    assert(sizeof(((ttmod_modinfo*)nullptr)->id) == 128);
    assert(sizeof(((ttmod_modinfo*)nullptr)->version) == 32);
    assert(TTMOD_MAX_EPISODES == 8);
    // Grants: known profile grants the API-backed subset in request order.
    {
        std::vector<Permission> req = {Permission::UI, Permission::GameWrite, Permission::GameEvents,
                                       Permission::ModsRead};
        auto g = ttmod::plugin_grants("mcsm1_pc_x86", req);
        assert(g.size() == 3);
        assert(g[0] == Permission::UI && g[1] == Permission::GameEvents && g[2] == Permission::ModsRead);
    }
    // Unknown/null profile grants nothing (fail-safe).
    {
        std::vector<Permission> req = {Permission::UI};
        assert(ttmod::plugin_grants("mcsm2_pc_x64", req).empty());
        assert(ttmod::plugin_grants(nullptr, req).empty());
        assert(ttmod::plugin_grants("", req).empty());
    }
    // Empty request grants empty (all shipped mods declare nothing).
    assert(ttmod::plugin_grants("mcsm1_pc_x86", {}).empty());
    // Runtimes gate: empty = legacy allowed; Native present = allowed;
    // non-empty without Native = contradictory DLL, refused.
    assert(ttmod::plugin_runtime_allowed({}));
    assert(ttmod::plugin_runtime_allowed({Runtime::Native}));
    assert(ttmod::plugin_runtime_allowed({Runtime::Lua, Runtime::Native}));
    assert(!ttmod::plugin_runtime_allowed({Runtime::Lua}));
    assert(!ttmod::plugin_runtime_allowed({Runtime::Lua, Runtime::Luau, Runtime::TelltaleLua}));
    printf("plugin-abi: version/layout/grants OK\n");
    return 0;
}
