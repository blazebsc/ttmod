#include "ttmod/modplan.hpp"
#include <algorithm>

namespace ttmod {

bool ModPlan::blocked(const ModId& id) const {
    for (auto& p : problems)
        if (p.mod == id) return true;
    return false;
}

std::string ModPlan::blocked_reason(const ModId& id) const {
    for (auto& p : problems)
        if (p.mod == id) return p.message;
    return "";
}

const LoadedMod* ModPlan::find(const ModId& id) const {
    for (auto& m : load_order)
        if (m.id == id) return &m;
    return nullptr;
}

ModPlan build_plan(const Discovery& disc, const CacheSync& cache, const ModPlanOptions& opt) {
    ModPlan plan;
    plan.disabled = disc.disabled;
    plan.invalid = disc.invalid;
    plan.skipped = disc.skipped;
    plan.entries_seen = disc.entries_seen;

    // Resolve each discovered mod to its effective directory once. A
    // packaged mod whose cache sync failed is dropped here (the sync already
    // logged the reason) rather than silently pointing at a missing dir.
    std::vector<LoadedMod> resolved;
    for (auto& d : disc.mods) {
        LoadedMod m;
        m.id = d.id;
        m.manifest = d.manifest;
        m.packaged = d.packaged;
        m.dir = d.source;
        if (d.packaged) {
            auto it = cache.effective.find(d.id.str());
            if (it == cache.effective.end()) {
                plan.skipped.push_back(d.id.str() + ": cache unavailable, not loaded");
                continue;
            }
            m.dir = it->second;
        }
        resolved.push_back(std::move(m));
    }

    // Safe mode blocks every mod through the same machinery as a dependency
    // failure, so there is exactly one "is this loadable" query downstream.
    if (opt.all_blocked) {
        for (auto& m : resolved) plan.problems.push_back({m.id, "safemode", "disabled by safe mode"});
    }

    // One resolution, over the mods that can actually be considered.
    std::vector<ModManifest> present;
    for (auto& m : resolved)
        if (!plan.blocked(m.id)) present.push_back(m.manifest);
    DepResolution dep = resolve_dependencies(present);
    for (auto& p : dep.problems) plan.problems.push_back(p);

    // load_order from the graph decides the sequence; look each id up in the
    // resolved set. Blocked mods never appear.
    for (auto& id : dep.load_order) {
        if (plan.blocked(id)) continue;
        for (auto& m : resolved)
            if (m.id == id) {
                plan.load_order.push_back(std::move(m));
                break;
            }
    }
    // Keep blocked-but-valid mods visible for the menu, id-sorted.
    for (auto& m : resolved) {
        if (!plan.blocked(m.id)) continue;
        plan.blocked_mods.push_back(std::move(m));
    }
    std::sort(plan.blocked_mods.begin(), plan.blocked_mods.end(),
              [](const LoadedMod& a, const LoadedMod& b) { return a.id < b.id; });
    return plan;
}

} // namespace ttmod