#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ttmod {

// One mod's override declarations (from its manifest; paths raw).
struct ModDef {
    std::string id;
    std::string dir; // absolute, native separators
    int priority = 100;
    bool enabled = true;
    // (game_path as written in manifest, replacement rel to mod dir)
    std::vector<std::pair<std::string, std::string>> files;
};

struct ResolveResult {
    bool found = false;
    std::string replacement; // normalized abs (internal '/' seps) when found
    std::string winner;
    int priority = 0;
    std::vector<std::string> shadowed; // other enabled claimants, high->low
    std::string reason; // "hit", "miss", "outside-root", ...
};

// Deterministic file-override index.
// - Keys are game-root-relative normalized paths.
// - Only requests under the game root are eligible (scope policy).
// - Highest priority wins; ties break by smallest mod id (documented).
// - Existence of replacements is checked at index time via exists().
// - Hot path resolve() does map lookup only (no IO, minimal allocs).
class Resolver {
public:
    using ExistsFn = std::function<bool(const std::string& norm_abs)>;

    void set_game_root(const std::string& abs_native);
    // Returns false + records problem when the mod contributes nothing usable
    // (still safe to ignore the return; problems() explains).
    bool add_mod(const ModDef& mod, ExistsFn exists);
    ResolveResult resolve(const std::string& requested_native) const;

    const std::vector<std::string>& problems() const { return problems_; }
    size_t override_count() const { return index_.size(); }

private:
    struct Entry {
        std::string replacement;
        std::string mod;
        int priority = 0;
    };
    std::string root_;
    std::map<std::string, std::vector<Entry>> index_; // key -> claimants
    std::vector<std::string> problems_;
};

} // namespace ttmod
