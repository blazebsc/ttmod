#pragma once
// ModPlan: the single answer to "which mods load, in what order, and why
// not" (Steps 5-7).
//
// Before this, the Windows runtime resolved dependencies twice - once in
// the resource indexer, once in the plugin loader - over two separately
// built mod lists, then sorted each by id and loaded whatever survived.
// Two resolutions meant two chances to disagree about which mods are
// loadable, and load order was "sort by id", not "dependencies first".
//
// Now discovery feeds resolve_dependencies() exactly once, and every
// consumer (resource index, plugin loader, script loader, menu) reads this
// one plan. resolve_dependencies() still loads nothing: it only computes.
//
// Determinism is the contract. For a given mods/ directory the plan is
// byte-identical across runs: discovery is id-sorted, the graph traverses
// id-ordered, and load order is topological.
#include <string>
#include <vector>

#include "ttmod/cache.hpp"
#include "ttmod/discovery.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/modgraph.hpp"
#include "ttmod/modid.hpp"

namespace ttmod {

// A validated mod with its effective on-disk location resolved. Everything
// here has passed manifest validation and dependency resolution.
struct LoadedMod {
    ModId id;
    std::string dir; // effective dir: unpacked dir or ttmod/cache/<id>
    bool packaged = false;
    ModManifest manifest;
};

struct ModPlan {
    // Dependency-first, id-deterministic. Blocked mods are excluded (they
    // cannot load); blocked() explains why.
    std::vector<LoadedMod> load_order;
    // Blocked mods, still id-sorted, so the menu can show them as broken.
    std::vector<LoadedMod> blocked_mods;
    // Valid but disabled (menu-visible, never loaded).
    std::vector<Discovered> disabled;
    // Unparseable entries (CLI-visible).
    std::vector<InvalidEntry> invalid;
    // Discovery-level skips: "id: reason".
    std::vector<std::string> skipped;
    // Dependency problems, including mods that are blocked.
    std::vector<DepProblem> problems;
    int entries_seen = 0;

    [[nodiscard]] bool blocked(const ModId& id) const;
    [[nodiscard]] std::string blocked_reason(const ModId& id) const;
    [[nodiscard]] const LoadedMod* find(const ModId& id) const;
};

struct ModPlanOptions {
    // Safe mode / TTMOD_SAFE_MODE: every third-party mod is blocked, but
    // discovery still runs so the user can see what is installed.
    bool all_blocked = false;
};

// Build the plan from canonical discovery plus a cache sync. Does no I/O of
// its own beyond what the arguments already produced.
ModPlan build_plan(const Discovery& disc, const CacheSync& cache, const ModPlanOptions& opt = {});

} // namespace ttmod