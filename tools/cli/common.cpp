// Shared CLI helpers: display shaping over core scan/inspect/parse.
#include "common.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "ttmod/discovery.hpp"
#include "ttmod/file_io.hpp"

namespace fs = std::filesystem;

std::vector<Listed> scan_mods_dir(const std::string& gamedir) {
    std::vector<Listed> out;
    std::string mods = (fs::path(gamedir) / "mods").string();
    for (auto& src : ttmod::scan_mod_sources(mods)) {
        auto result = ttmod::read_source_manifest(src);
        const bool is_dir = src.kind == ttmod::ModSourceKind::Directory;
        const std::string label = src.name + (is_dir ? "/" : "");
        if (!result.ok()) {
            out.push_back({"?", "?", label + " [INVALID: " + result.error().message + "]", "",
                           ttmod::ModManifest{}, false});
            continue;
        }
        auto manifest = std::move(result).value();
        if (!manifest) continue;
        auto m = std::move(*manifest);
        out.push_back({m.identity.id.str(), m.identity.version.str(), label, "", std::move(m)});
    }
    std::sort(out.begin(), out.end(), [](const Listed& a, const Listed& b) { return a.id < b.id; });
    return out;
}

ttmod::ModState load_state(const std::string& gamedir) {
    ttmod::ModState st;
    std::string p = (fs::path(gamedir) / "config" / "mods.json").string();
    auto f = ttmod::parse_state(ttmod::file_io::read_all(p));
    if (f.ok()) st = f.value();
    return st;
}

bool save_state(const std::string& gamedir, const ttmod::ModState& st) {
    std::error_code ec;
    fs::create_directories(fs::path(gamedir) / "config", ec);
    return ttmod::file_io::write_file_atomic(
        ((fs::path(gamedir) / "config" / "mods.json").string()), st.serialize() + "\n");
}
