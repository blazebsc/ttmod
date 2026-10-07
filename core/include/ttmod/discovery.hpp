#pragma once
#include <optional>
#include <string>
#include <vector>
#include "ttmod/manifest.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/result.hpp"

namespace ttmod {

// Canonical drop-in discovery for <game>/mods (portable, tested).
// Recognizes: *.ttmod (validated, manifest read from ZIP) and unpacked dirs
// containing manifest.json. Ignores everything else (README, screenshots,
// random DLLs, non-.ttmod zips, dotfiles).
// Applies ModState (disabled -> skipped, recorded) and validates api/game.
// Duplicate IDs: unpacked dir wins over .ttmod (dev override), rest rejected;
// all decisions recorded in skipped[] as user-friendly reasons.
enum class ModSourceKind { Directory, Package };

// ModSource describes origin (where); ModManifest describes identity/content
// (what). A cache directory is an effective location for a Package source,
// not a source.
struct ModSource {
    std::string name; // filename/dirname
    std::string path; // absolute path
    ModSourceKind kind;
};

// One directory walk, shared by runtime discovery and the CLI: every
// *.ttmod file and every subdirectory (hidden/OS-metadata skipped), sorted
// by name. Junk (README, DLLs, zips) never enters. Callers validate.
std::vector<ModSource> scan_mod_sources(const std::string& mods_dir, int* entries_seen = nullptr);

// The one place that turns a source into a manifest.
// ok(nullopt): not a mod (Directory without manifest.json).
// fail: Package -> operation "inspect-package" (bad archive) or the manifest's
//       own error; Directory -> manifest read/parse/validate error. An EMPTY
//       manifest.json is a failure now, not "not a mod".
Result<std::optional<ModManifest>> read_source_manifest(const ModSource& src);

struct Discovered {
    ModId id;
    ModSource source;
    ModManifest manifest;
    [[nodiscard]] bool packaged() const noexcept { return source.kind == ModSourceKind::Package; }
};

// Invalid entries stay visible (CLI lists them) instead of vanishing.
struct InvalidEntry {
    std::string source; // absolute path
    std::string reason;
};

struct Discovery {
    std::vector<Discovered> mods;      // id-sorted, deduplicated, enabled only
    std::vector<Discovered> disabled;  // valid but disabled (for menus)
    std::vector<InvalidEntry> invalid; // unparseable (for CLI display)
    std::vector<std::string> skipped;  // "id: reason" (or filename when id unknown)
    int entries_seen = 0;
};

Discovery discover_mods(const std::string& mods_dir, const ModState& state, const char* game, int season);

} // namespace ttmod
