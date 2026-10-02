#pragma once
#include <map>
#include <string>

namespace ttmod {

// Framework-side enable state (config/mods.json). The PACKAGE manifest
// describes the mod; this file describes the user's choice. Never modifies
// distributed packages. Missing entry = manifest default (enabled field).
// {
//   "some.mod": {"enabled": false}
// }
struct ModState {
    // id -> enabled override present in the file
    std::map<std::string, bool> overrides;
    bool enabled_for(const std::string& id, bool manifest_default) const;
    void set(const std::string& id, bool enabled);
    std::string serialize() const; // deterministic (sorted keys)
};

// Parses mods.json; tolerant: blank -> empty state; garbage -> ok=false.
// Entries without an "enabled" bool are ignored (unknown future fields in
// other entries are skipped, keeping parse forward-compatible).
struct StateFile {
    bool ok = true;
    std::string error;
    ModState state;
};

StateFile parse_state(const std::string& text);

} // namespace ttmod
