// Shared CLI helpers: display shaping over core scan/inspect/parse.
#include "common.hpp"

#include <algorithm>
#include <filesystem>

#include "ttmod/discovery.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/package.hpp"

namespace fs = std::filesystem;

std::vector<Listed> scan_mods_dir(const std::string& gamedir) {
    std::vector<Listed> out;
    std::string mods = (fs::path(gamedir) / "mods").string();
    for (auto& src : ttmod::scan_mod_sources(mods)) {
        if (src.kind == ttmod::ModSourceKind::Package) {
            auto insp = ttmod::inspect_package(src.path);
            if (!insp.ok()) {
                ttmod::ModManifest bad;
                out.push_back(
                    {"?", "?", src.name + " [INVALID: " + insp.error().message + "]", "", bad, false});
                continue;
            }
            auto pm = ttmod::parse_manifest(insp.value().manifest_text);
            if (!pm.ok()) continue; // inspect validated; defensive
            auto m = pm.value();
            out.push_back({m.identity.id, m.identity.version, src.name, "", m});
        } else {
            std::string mf = (fs::path(src.path) / "manifest.json").string();
            std::error_code ec;
            if (!fs::exists(mf, ec)) continue; // not a mod dir
            auto pm = ttmod::parse_manifest(ttmod::file_io::read_all(mf));
            if (!pm.ok()) {
                ttmod::ModManifest bad;
                out.push_back({"?", "?", src.name + "/ [INVALID: " + pm.error().message + "]", "", bad,
                               false});
                continue;
            }
            auto m = pm.value();
            out.push_back({m.identity.id, m.identity.version, src.name + "/", "", m});
        }
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
