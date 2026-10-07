// Canonical mods/ discovery. See discovery.hpp.
#include "ttmod/discovery.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/package.hpp"
#include "ttmod/plugin_api.h"

#include <algorithm>
#include <filesystem>

namespace ttmod {
namespace {

namespace fs = std::filesystem;

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
            if (e.path().extension() == ".ttmod") out.push_back({name, e.path().string(), ModSourceKind::Package});
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

Result<std::optional<ModManifest>> read_source_manifest(const ModSource& src) {
    std::string manifest_text;
    if (src.kind == ModSourceKind::Directory) {
        const std::string manifest_path = src.path + "/manifest.json";
        if (!file_io::exists(manifest_path)) return Result<std::optional<ModManifest>>::ok(std::nullopt);

        FILE* f = file_io::open_read(manifest_path);
        if (!f) {
            return Result<std::optional<ModManifest>>::fail(
                Error{"read-manifest", manifest_path, errcat::kIO, "cannot read manifest.json"});
        }
        char buffer[4096];
        size_t count;
        while ((count = fread(buffer, 1, sizeof buffer, f)) > 0) manifest_text.append(buffer, count);
        const bool read_error = ferror(f) != 0;
        fclose(f);
        if (read_error) {
            return Result<std::optional<ModManifest>>::fail(
                Error{"read-manifest", manifest_path, errcat::kIO, "cannot read manifest.json"});
        }
    } else {
        auto inspected = inspect_package(src.path);
        if (!inspected.ok()) return Result<std::optional<ModManifest>>::fail(inspected.error());
        manifest_text = inspected.value().manifest_text;
    }

    auto parsed = parse_manifest(manifest_text);
    if (!parsed.ok()) return Result<std::optional<ModManifest>>::fail(parsed.error());
    return Result<std::optional<ModManifest>>::ok(std::optional<ModManifest>(std::move(parsed).value()));
}

Discovery discover_mods(const std::string& mods_dir, const ModState& state, const char* game, int season) {
    Discovery d;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) return d;
    struct Cand {
        ModId id;
        ModSource source;
        ModManifest manifest;
    };
    std::vector<Cand> cands;
    for (auto& src : scan_mod_sources(mods_dir, &d.entries_seen)) {
        auto result = read_source_manifest(src);
        if (!result.ok()) {
            if (src.kind == ModSourceKind::Package && result.error().operation == "inspect-package")
                d.skipped.push_back(src.name + ": invalid package: " + result.error().message);
            else d.invalid.push_back({src.path, src.name + ": invalid manifest: " + result.error().message});
            continue;
        }
        auto manifest = std::move(result).value();
        if (!manifest) continue;
        ModManifest value = std::move(*manifest);
        cands.push_back({value.identity.id, src, std::move(value)});
    }
    // Validate api/game, apply state, dedupe (unpacked dir beats package).
    struct Item {
        Discovered disc;
    };
    std::vector<Item> items;
    for (auto& c : cands) {
        const ModManifest& m = c.manifest;
        if (!m.compat.supports_api(TTMOD_PLUGIN_API_VERSION)) {
            d.skipped.push_back(c.id.str() + ": unsupported API " + std::to_string(m.compat.api));
            continue;
        }
        if (!m.compat.supports_game(game, season)) {
            d.skipped.push_back(c.id.str() + ": game not supported");
            continue;
        }
        if (!effective_enabled(m, state)) {
            d.skipped.push_back(c.id.str() + ": disabled");
            d.disabled.push_back({c.id, c.source, m}); // menu-visible
            continue;
        }
        items.push_back({{c.id, c.source, m}});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.disc.id != b.disc.id) return a.disc.id < b.disc.id;
        const bool a_dir = a.disc.source.kind == ModSourceKind::Directory;
        const bool b_dir = b.disc.source.kind == ModSourceKind::Directory;
        return a_dir > b_dir; // unpacked dir first on ties
    });
    ModId kept_id;
    std::string kept_kind;
    for (size_t i = 0; i < items.size(); ++i) {
        if (kept_id.valid() && items[i].disc.id == kept_id) {
            d.skipped.push_back(items[i].disc.id.str() + ": duplicate ID (kept " + kept_kind + ")");
            continue;
        }
        kept_id = items[i].disc.id;
        kept_kind = items[i].disc.source.kind == ModSourceKind::Directory ? "directory" : "package";
        d.mods.push_back(items[i].disc);
    }
    std::sort(d.disabled.begin(), d.disabled.end(),
              [](const Discovered& a, const Discovered& b) { return a.id < b.id; });
    // Mods are id-sorted by construction (items sorted before dedupe).
    return d;
}

} // namespace ttmod
