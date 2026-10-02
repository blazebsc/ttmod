// Canonical mods/ discovery. See discovery.hpp.
#include "ttmod/discovery.hpp"
#include "ttmod/package.hpp"
#include "ttmod/plugin_api.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace ttmod {
namespace {

namespace fs = std::filesystem;

std::string read_file(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char b[4096];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) s.append(b, r);
    fclose(f);
    return s;
}

bool game_ok(const ModManifest& m, const char* game, int season) {
    char want[128];
    snprintf(want, sizeof want, "%s:s%d", game, season);
    for (auto& g : m.games)
        if (g == want || g == game) return true;
    return false;
}

} // namespace

Discovery discover_mods(const std::string& mods_dir, const ModState& state, const char* game,
                        int season) {
    Discovery d;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) return d;
    struct Cand {
        std::string id, source, manifest_text;
        bool packaged = false;
    };
    std::vector<Cand> cands;
    std::vector<std::pair<std::string, std::string>> dir_entries; // (name, path) sorted
    std::vector<std::string> pack_entries;
    for (auto& e : fs::directory_iterator(mods_dir, ec)) {
        if (ec) break;
        d.entries_seen++;
        std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue; // hidden/OS metadata
        if (e.is_regular_file(ec)) {
            if (e.path().extension() == ".ttmod") pack_entries.push_back(e.path().string());
            // else: README/screenshots/random DLLs/zips ignored silently
        } else if (e.is_directory(ec)) {
            dir_entries.emplace_back(name, e.path().string());
        }
    }
    std::sort(dir_entries.begin(), dir_entries.end());
    std::sort(pack_entries.begin(), pack_entries.end());
    for (auto& [name, path] : dir_entries) {
        std::string mt = read_file(path + "/manifest.json");
        if (mt.empty()) continue; // not a mod dir, ignore silently
        ModManifest m = parse_manifest(mt);
        if (!m.ok) {
            d.skipped.push_back(name + ": invalid manifest: " + m.error);
            continue;
        }
        cands.push_back({m.id, path, mt, false});
    }
    for (auto& path : pack_entries) {
        std::string base = fs::path(path).filename().string();
        PackView v = inspect_package(path);
        if (!v.ok) {
            d.skipped.push_back(base + ": invalid package: " + v.error);
            continue;
        }
        ModManifest m = parse_manifest(v.manifest_text);
        if (!m.ok) { // inspect already validated; defensive
            d.skipped.push_back(base + ": invalid manifest: " + m.error);
            continue;
        }
        cands.push_back({m.id, path, v.manifest_text, true});
    }
    // Validate api/game, apply state, dedupe (unpacked dir beats package).
    struct Item {
        Discovered disc;
        bool is_dir;
    };
    std::vector<Item> items;
    for (auto& c : cands) {
        ModManifest m = parse_manifest(c.manifest_text);
        if (m.api < 1 || m.api > TTMOD_PLUGIN_API_VERSION) {
            d.skipped.push_back(c.id + ": unsupported API " + std::to_string(m.api));
            continue;
        }
        if (!game_ok(m, game, season)) {
            d.skipped.push_back(c.id + ": game not supported");
            continue;
        }
        if (!state.enabled_for(c.id, m.enabled)) {
            d.skipped.push_back(c.id + ": disabled");
            d.disabled.push_back({c.id, c.source, c.packaged, m}); // menu-visible
            continue;
        }
        items.push_back({{c.id, c.source, c.packaged, m}, !c.packaged});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.disc.id != b.disc.id) return a.disc.id < b.disc.id;
        return a.is_dir > b.is_dir; // unpacked dir first on ties
    });
    std::string kept_id, kept_kind;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!kept_id.empty() && items[i].disc.id == kept_id) {
            d.skipped.push_back(items[i].disc.id + ": duplicate ID (kept " + kept_kind + ")");
            continue;
        }
        kept_id = items[i].disc.id;
        kept_kind = items[i].is_dir ? "directory" : "package";
        d.mods.push_back(items[i].disc);
    }
    std::sort(d.disabled.begin(), d.disabled.end(),
              [](const Discovered& a, const Discovered& b) { return a.id < b.id; });
    return d;
}

} // namespace ttmod
