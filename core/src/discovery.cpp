// Canonical mods/ discovery. See discovery.hpp.
#include "ttmod/discovery.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/package.hpp"
#include "ttmod/plugin_api.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace ttmod {
namespace {

namespace fs = std::filesystem;

std::string read_file(const std::string& p) {
    FILE* f = ttmod::file_io::open_read(p);
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
    for (auto& g : m.compat.games)
        if (g == want || g == game) return true;
    return false;
}

} // namespace

std::vector<ModSource> scan_mod_sources(const std::string& mods_dir, int* entries_seen) {
    std::vector<ModSource> out;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) return out;
    for (auto& e : fs::directory_iterator(mods_dir, ec)) {
        if (ec) break;
        if (entries_seen) ++*entries_seen;
        std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '.') continue; // hidden/OS metadata
        if (e.is_regular_file(ec)) {
            if (e.path().extension() == ".ttmod")
                out.push_back({name, e.path().string(), ModSourceKind::Package});
            // else: README/screenshots/random DLLs/zips ignored silently
        } else if (e.is_directory(ec)) {
            out.push_back({name, e.path().string(), ModSourceKind::Directory});
        }
    }
    std::sort(out.begin(), out.end(), [](const ModSource& a, const ModSource& b) {
        if (a.kind != b.kind) return a.kind == ModSourceKind::Directory;
        return a.name < b.name;
    });
    return out;
}

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
    for (auto& src : scan_mod_sources(mods_dir, &d.entries_seen)) {
        if (src.kind == ModSourceKind::Directory) {
            std::string mt = read_file(src.path + "/manifest.json");
            if (mt.empty()) continue; // not a mod dir, ignore silently
            Result<ModManifest> pm = parse_manifest(mt);
            if (!pm.ok()) {
                d.invalid.push_back({src.path, src.name + ": invalid manifest: " + pm.error().message});
                continue;
            }
            ModManifest m = pm.value();
            cands.push_back({m.identity.id, src.path, mt, false});
        } else {
            PackView v = inspect_package(src.path);
            if (!v.ok) {
                d.skipped.push_back(src.name + ": invalid package: " + v.error);
                continue;
            }
            Result<ModManifest> pm = parse_manifest(v.manifest_text);
            if (!pm.ok()) { // inspect already validated; defensive
                d.invalid.push_back({src.path, src.name + ": invalid manifest: " + pm.error().message});
                continue;
            }
            ModManifest m = pm.value();
            cands.push_back({m.identity.id, src.path, v.manifest_text, true});
        }
    }
    // Validate api/game, apply state, dedupe (unpacked dir beats package).
    struct Item {
        Discovered disc;
        bool is_dir;
    };
    std::vector<Item> items;
    for (auto& c : cands) {
        ModManifest m = parse_manifest(c.manifest_text).value();
        if (m.compat.api < 1 || m.compat.api > TTMOD_PLUGIN_API_VERSION) {
            d.skipped.push_back(c.id + ": unsupported API " + std::to_string(m.compat.api));
            continue;
        }
        if (!game_ok(m, game, season)) {
            d.skipped.push_back(c.id + ": game not supported");
            continue;
        }
        if (!effective_enabled(m, state)) {
            d.skipped.push_back(c.id + ": disabled");
            d.disabled.push_back({c.id, c.source, c.packaged, ModSourceKind::Directory, m}); // menu-visible
            continue;
        }
        items.push_back({{c.id, c.source, c.packaged, c.packaged ? ModSourceKind::Package : ModSourceKind::Directory, m}, !c.packaged});
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
