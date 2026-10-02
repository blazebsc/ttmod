#pragma once
#include <string>
#include <vector>
#include "ttmod/manifest.hpp"
#include "ttmod/modstate.hpp"

namespace ttmod {

// Canonical drop-in discovery for <game>/mods (portable, tested).
// Recognizes: *.ttmod (validated, manifest read from ZIP) and unpacked dirs
// containing manifest.json. Ignores everything else (README, screenshots,
// random DLLs, non-.ttmod zips, dotfiles).
// Applies ModState (disabled -> skipped, recorded) and validates api/game.
// Duplicate IDs: unpacked dir wins over .ttmod (dev override), rest rejected;
// all decisions recorded in skipped[] as user-friendly reasons.
struct Discovered {
    std::string id;
    std::string source; // absolute path: .ttmod file or unpacked dir
    bool packaged = false;
    ModManifest manifest;
};

struct Discovery {
    std::vector<Discovered> mods; // id-sorted, deduplicated, enabled only
    std::vector<Discovered> disabled; // valid but disabled (for menus)
    std::vector<std::string> skipped; // "id: reason" (or filename when id unknown)
    int entries_seen = 0;
};

Discovery discover_mods(const std::string& mods_dir, const ModState& state, const char* game,
                        int season);

} // namespace ttmod
