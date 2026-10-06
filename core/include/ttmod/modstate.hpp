#pragma once
#include <map>
#include <string>
#include "ttmod/result.hpp"

namespace ttmod {

// Framework-side enable state (config/mods.json). The PACKAGE manifest
// describes the mod; this file describes the user's choice. Never modifies
// distributed packages. Missing entry = manifest default (enabled field).
// {
//   "some.mod": {"enabled": false}
// }
struct ModManifest; // fwd (manifest.hpp); keeps this header light

struct ModState {
    // id -> enabled override present in the file
    std::map<std::string, bool> overrides;
    bool enabled_for(const std::string& id, bool manifest_default) const;
    void set(const std::string& id, bool enabled);
    std::string serialize() const; // deterministic (sorted keys)
};

// Authoritative enabled query: manifest `enabled` is the default, the
// mods.json override wins when present. All production readers (discovery,
// menu snapshot, CLI list/info, summary counter via Discovery::disabled)
// must use this; enabled_for() is the low-level primitive it delegates to.
// Log lines render this answer, never re-derive it (no substring counting).
bool effective_enabled(const ModManifest& manifest, const ModState& state);

// Parses mods.json; tolerant: blank -> empty state; garbage -> error.
// Entries without an "enabled" bool are ignored (unknown future fields in
// other entries are skipped, keeping parse forward-compatible).
Result<ModState> parse_state(const std::string& text);

} // namespace ttmod
