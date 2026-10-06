#include "ttmod/moddeps.hpp"
#include "ttmod/version.hpp"

namespace ttmod {

std::string check_requirements(const ModManifest& mod, const std::vector<ModManifest>& present) {
    for (auto& [dep, minver] : mod.deps.depends) {
        const ModManifest* found = nullptr;
        for (auto& p : present)
            if (p.identity.id == dep) {
                found = &p;
                break;
            }
        if (!found) return "missing dependency: " + dep;
        VersionConstraint need(minver);
        if (!need.satisfied_by(Version(found->identity.version)))
            return "dependency " + dep + " version " + found->identity.version + " < " + minver;
    }
    for (auto& c : mod.deps.conflicts)
        for (auto& p : present)
            if (p.identity.id == c) return "conflicts with present mod: " + c;
    return "";
}

} // namespace ttmod
