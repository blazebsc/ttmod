#include "ttmod/moddeps.hpp"

namespace ttmod {

std::string check_requirements(const ModManifest& mod, const std::vector<ModManifest>& present) {
    for (auto& [dep, minver] : mod.depends) {
        const ModManifest* found = nullptr;
        for (auto& p : present)
            if (p.id == dep) {
                found = &p;
                break;
            }
        if (!found) return "missing dependency: " + dep;
        if (!minver.empty() && compare_versions(found->version, minver) < 0)
            return "dependency " + dep + " version " + found->version + " < " + minver;
    }
    for (auto& c : mod.conflicts)
        for (auto& p : present)
            if (p.id == c) return "conflicts with present mod: " + c;
    return "";
}

} // namespace ttmod
