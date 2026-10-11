#pragma once
// Native plugin capability negotiation (roadmap PR 20).
//
// Manifests declare requested Permission values; the host grants the subset
// its ABI actually exposes a function for. Two load-bearing honesties:
//
// 1. LoadLibrary is arbitrary code: the host CANNOT confine a native plugin,
//    so "grant" never means confinement. It means "the host provides an API
//    for this" - declaration stays truthful without pretending a sandbox
//    that does not exist (the loader still logs the arbitrary-code WARNING
//    for every DLL).
// 2. Unknown profile grants nothing (fail-safe, same rule as profile_has).
//
// The runtimes gate is the one hard refusal: a manifest whose non-empty
// runtimes disclaim Native while shipping a DLL is contradictory - refuse
// it with a reason instead of executing code the author said isn't there.
// Empty runtimes = legacy/unspecified = allowed (all shipped mods).
#include <string>
#include <vector>

#include "ttmod/manifest.hpp"
#include "ttmod/runtime.hpp"

namespace ttmod {

// Permissions the v5 host ABI exposes a function for:
// GameRead -> get_state, GameEvents -> subscribe, UI -> queue_ui_chunk,
// ModsRead -> get_mod_count/get_mod_info, Resources -> file/archive/
// resdesc open events. Everything else has no host function behind it.
inline bool permission_has_host_api(Permission p) {
    switch (p) {
        case Permission::GameRead:
        case Permission::GameEvents:
        case Permission::UI:
        case Permission::Resources:
        case Permission::ModsRead:
            return true;
        default:
            return false;
    }
}

// Granted subset of `requested` for this profile, in request order.
inline std::vector<Permission> plugin_grants(const char* profile_id,
                                             const std::vector<Permission>& requested) {
    std::vector<Permission> out;
    if (!profile_has(profile_id, Capability::Plugins)) return out;
    for (auto p : requested)
        if (permission_has_host_api(p)) out.push_back(p);
    return out;
}

inline bool plugin_runtime_allowed(const std::vector<Runtime>& runtimes) {
    if (runtimes.empty()) return true;
    for (auto r : runtimes)
        if (r == Runtime::Native) return true;
    return false;
}

} // namespace ttmod
