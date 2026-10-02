#pragma once
#include <string>
#include <vector>
#include "ttmod/manifest.hpp"

namespace ttmod {

// M10 dependency/conflict gating over the unified present-mod set.
// present = enabled + api/game-valid manifests (any dir).
// Returns "" when loadable, else a human reason (missing dep, version too
// low, conflicting mod present). No auto-resolution (documented).
std::string check_requirements(const ModManifest& mod, const std::vector<ModManifest>& present);

} // namespace ttmod
