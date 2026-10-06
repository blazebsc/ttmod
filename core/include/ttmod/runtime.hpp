#pragma once
#include <string>
#include "ttmod/profile.hpp"

namespace ttmod {

// What a profile can do. Generic runtime code asks capabilities instead of
// comparing hardcoded profile IDs (Stage E).
enum class Capability { FileOverrides, Plugins, LuaBridge, Menu, Events, GameState };

// Today exactly one profile exists and it has everything; unknown builds
// get nothing (fail-safe: init returns early, game continues vanilla).
// The SET is the seam - adding a game means extending this function.
inline bool profile_has(const char* profile_id, Capability) {
    return profile_id && std::string(profile_id) == "mcsm1_pc_x86";
}

// Explicit runtime mode (Stage E): failure behavior with a name.
enum class RuntimeMode { Disabled, Detecting, Vanilla, Active, Degraded };

inline const char* to_string(RuntimeMode m) {
    switch (m) {
        case RuntimeMode::Disabled: return "disabled";
        case RuntimeMode::Detecting: return "detecting";
        case RuntimeMode::Vanilla: return "vanilla";
        case RuntimeMode::Active: return "active";
        default: return "degraded";
    }
}

// Mode from detection outcome. Unknown executable or known-but-unsupported
// build -> Vanilla (game continues untouched). Partial init failure is
// decided by the caller -> Degraded.
inline RuntimeMode mode_for_status(ProfileStatus s) {
    return s == ProfileStatus::Supported ? RuntimeMode::Active : RuntimeMode::Vanilla;
}

} // namespace ttmod
