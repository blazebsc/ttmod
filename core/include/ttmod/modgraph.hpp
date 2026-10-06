#pragma once
#include <string>
#include <vector>
#include "ttmod/manifest.hpp"
#include "ttmod/modid.hpp"

namespace ttmod {

// Explicit dependency graph over one present-mod set (Stage D). Replaces
// per-mod string checks with a single resolution: missing dependencies,
// version constraints, conflicts, duplicate IDs, and cycles are all
// reported, and the load order is topological (dependencies first) and
// deterministic (id-sorted input, id-ordered traversal).
struct DepProblem {
    ModId mod;            // the mod that cannot load
    std::string category; // "missing", "version", "conflict", "duplicate", "cycle"
    std::string message;  // human reason, for logs
};

struct DepResolution {
    std::vector<ModId> load_order; // dependency-first; blocked mods appended last by id
    std::vector<DepProblem> problems;
    bool blocked(const ModId& id) const;
};

// mods: enabled + api/game-valid manifests. Duplicate IDs: first wins,
// rest flagged. Conflict = conflicting id present in the same set.
DepResolution resolve_dependencies(const std::vector<ModManifest>& mods);

} // namespace ttmod
