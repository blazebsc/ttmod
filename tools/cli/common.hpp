#pragma once
// Shared CLI helpers (Stage 19): display shaping over core APIs. The CLI
// lists invalid entries and skips no game filter - that display policy
// lives here, not in core discovery.
#include <string>
#include <vector>

#include "ttmod/manifest.hpp"
#include "ttmod/modstate.hpp"

struct Listed {
    std::string id, version, src, note;
    ttmod::ModManifest m;
};

// <gamedir>/mods: *.ttmod (inspect) + */manifest.json, id-sorted.
// Walk implementation is core scan_mod_sources; display shaping is here.
std::vector<Listed> scan_mods_dir(const std::string& gamedir);

ttmod::ModState load_state(const std::string& gamedir);
bool save_state(const std::string& gamedir, const ttmod::ModState& st);
void print_packinfo(const std::string& path);
