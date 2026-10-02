#include "ttmod/resolver.hpp"
#include "ttmod/pathnorm.hpp"
#include <algorithm>

namespace ttmod {

void Resolver::set_game_root(const std::string& abs_native) {
    root_ = normalize_win_path(abs_native);
}

bool Resolver::add_mod(const ModDef& mod, ExistsFn exists) {
    if (!mod.enabled) return false; // disabled: invisible to the index
    if (mod.id.empty()) {
        problems_.push_back("mod rejected: empty id");
        return false;
    }
    std::string mdir = normalize_win_path(mod.dir);
    bool any = false;
    for (auto& [gp, rel] : mod.files) {
        std::string key_src = gp;
        // Manifest game paths may be root-relative ("archives/x") or absolute;
        // absolutize against the game root for keying when relative.
        std::string key;
        if (key_src.size() > 1 && key_src[1] == ':') {
            auto k = relative_key(normalize_win_path(key_src), root_);
            if (!k) {
                problems_.push_back("mod " + mod.id + ": game path outside root: " + gp);
                continue;
            }
            key = *k;
        } else {
            key = normalize_win_path(key_src);
        }
        auto rep = join_checked(mdir, rel);
        if (!rep) {
            problems_.push_back("mod " + mod.id + ": replacement escapes mod dir: " + rel);
            continue;
        }
        if (exists && !exists(*rep)) {
            problems_.push_back("mod " + mod.id + ": replacement missing: " + rel);
            continue;
        }
        index_[key].push_back(Entry{*rep, mod.id, mod.priority});
        any = true;
    }
    // Deterministic claimant order: priority desc, id asc.
    for (auto& [k, v] : index_)
        std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.mod < b.mod;
        });
    return any;
}

ResolveResult Resolver::resolve(const std::string& requested_native) const {
    ResolveResult r;
    if (root_.empty()) {
        r.reason = "no-root";
        return r;
    }
    auto key = relative_key(normalize_win_path(requested_native), root_);
    if (!key) {
        r.reason = "outside-root";
        return r;
    }
    auto it = index_.find(*key);
    if (it == index_.end() || it->second.empty()) {
        r.reason = "miss";
        return r;
    }
    const Entry& win = it->second.front();
    r.found = true;
    r.replacement = win.replacement;
    r.winner = win.mod;
    r.priority = win.priority;
    for (size_t i = 1; i < it->second.size(); ++i) r.shadowed.push_back(it->second[i].mod);
    r.reason = "hit";
    return r;
}

} // namespace ttmod
