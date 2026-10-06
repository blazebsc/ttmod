#include "ttmod/modgraph.hpp"
#include "ttmod/version.hpp"
#include <algorithm>
#include <functional>
#include <map>

namespace ttmod {

bool DepResolution::blocked(const ModId& id) const {
    for (auto& p : problems)
        if (p.mod == id) return true;
    return false;
}

DepResolution resolve_dependencies(const std::vector<ModManifest>& mods) {
    DepResolution out;
    // Index by id (input is id-sorted from discovery; first wins).
    std::map<ModId, const ModManifest*> by_id;
    for (auto& m : mods) {
        const ModId& id = m.identity.id;
        if (by_id.count(id)) {
            out.problems.push_back({id, "duplicate", "duplicate ID, kept first"});
            continue;
        }
        by_id[id] = &m;
    }
    // Blocked set: missing/version/conflict/cycle. Iterate to a fixed point
    // so knock-on blocks (dep of a blocked mod) propagate.
    std::map<ModId, bool> blocked;
    for (auto& [id, _] : by_id) blocked[id] = false;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [id, m] : by_id) {
            if (blocked[id]) continue;
            auto block = [&](const std::string& cat, const std::string& msg) {
                blocked[id] = true;
                out.problems.push_back({id, cat, msg});
                changed = true;
            };
            for (auto& [dep, spec] : m->deps.depends) {
                auto it = by_id.find(dep);
                if (it == by_id.end()) {
                    block("missing", "missing dependency: " + dep.str());
                    break;
                }
                if (blocked[dep]) {
                    block("missing", "dependency blocked: " + dep.str());
                    break;
                }
                // Constraint was validated at the manifest boundary; parse again here so
                // the graph depends on the type, not on a raw string.
                auto need = VersionConstraint::parse(spec);
                if (!need.ok() || !need.value().satisfied_by(Version::parse(it->second->identity.version).value())) {
                    block("version",
                          "dependency " + dep.str() + " version " + it->second->identity.version + " < " + spec);
                    break;
                }
            }
            if (blocked[id]) continue;
            for (auto& c : m->deps.conflicts) {
                if (by_id.count(c)) {
                    block("conflict", "conflicts with present mod: " + c.str());
                    break;
                }
            }
        }
    }
    // Cycle detection over unblocked edges (iterative DFS, id-ordered).
    // Only the members of each cycle are blocked, not their ancestors.
    std::map<ModId, int> color; // 0 unvisited, 1 in-stack, 2 done
    std::vector<ModId> stack;
    std::function<void(const ModId&)> visit = [&](const ModId& id) {
        color[id] = 1;
        stack.push_back(id);
        auto it = by_id.find(id);
        if (it != by_id.end()) {
            std::vector<ModId> deps;
            for (auto& [dep, _] : it->second->deps.depends) deps.push_back(dep);
            std::sort(deps.begin(), deps.end());
            for (auto& dep : deps) {
                if (!by_id.count(dep) || blocked[dep]) continue;
                if (color[dep] == 1) {
                    bool in_cycle = false;
                    for (auto& mem : stack) {
                        if (mem == dep) in_cycle = true;
                        if (in_cycle && !blocked[mem]) {
                            blocked[mem] = true;
                            out.problems.push_back({mem, "cycle", "dependency cycle"});
                        }
                    }
                } else if (color[dep] == 0) {
                    visit(dep);
                }
            }
        }
        stack.pop_back();
        color[id] = 2;
    };
    for (auto& [id, _] : by_id)
        if (color[id] == 0 && !blocked[id]) visit(id);
    // Topological load order (deps first), id-deterministic; blocked last.
    std::map<ModId, bool> emitted;
    std::function<void(const ModId&)> emit = [&](const ModId& id) {
        if (emitted[id]) return;
        emitted[id] = true;
        auto it = by_id.find(id);
        if (it != by_id.end() && !blocked[id]) {
            std::vector<ModId> deps;
            for (auto& [dep, _] : it->second->deps.depends) deps.push_back(dep);
            std::sort(deps.begin(), deps.end());
            for (auto& dep : deps)
                if (by_id.count(dep) && !blocked[dep]) emit(dep);
        }
        out.load_order.push_back(id);
    };
    for (auto& [id, _] : by_id)
        if (!blocked[id]) emit(id);
    for (auto& [id, _] : by_id)
        if (blocked[id] && !emitted[id]) emit(id);
    return out;
}

} // namespace ttmod