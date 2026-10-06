// ttmod CLI (portable dev/advanced tool; normal users just drop mods/).
//   ttmod detect <exe>
//   ttmod package create <moddir> <out.ttmod>
//   ttmod package validate <file.ttmod>
//   ttmod package info <file.ttmod>
//   ttmod mods list <gamedir>
//   ttmod mods info <gamedir> <id>
//   ttmod mods enable|disable <gamedir> <id>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "ttmod/detect.hpp"
#include "ttmod/discovery.hpp"
#include "ttmod/logmap.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/package.hpp"
#include "ttmod/profile.hpp"
#include "ttmod/ttarch.hpp"

namespace fs = std::filesystem;
namespace {

int usage() {
    std::puts("usage: ttmod <detect|package|inspect|map> ...\n"
              "  detect <exe>\n"
              "  package create <moddir> <out.ttmod>\n"
              "  package validate <file>\n"
              "  package info <file>\n"
              "  inspect <file.ttarch2>\n"
              "  map <ttmod.log>");
    return 2;
}

struct Listed {
    std::string id, version, src, note;
    ttmod::ModManifest m;
};

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
bool write_file(const std::string& p, const std::string& s) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    size_t w = fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    return w == s.size();
}

// Scan <gamedir>/mods: *.ttmod (inspect) + */manifest.json. Sorted by id.
// Single walk implementation lives in core (scan_mod_sources); this only
// shapes entries for display (invalid stays visible, no game filter).
std::vector<Listed> scan_mods_dir(const std::string& gamedir) {
    std::vector<Listed> out;
    std::string mods = (fs::path(gamedir) / "mods").string();
    for (auto& src : ttmod::scan_mod_sources(mods)) {
        if (src.kind == ttmod::ModSourceKind::Package) {
            auto v = ttmod::inspect_package(src.path);
            if (!v.ok) {
                ttmod::ModManifest bad;
                bad.error = v.error;
                out.push_back({"?", "?", src.name + " [INVALID: " + v.error + "]", "", bad});
                continue;
            }
            auto m = ttmod::parse_manifest(v.manifest_text);
            out.push_back({m.identity.id, m.identity.version, src.name, "", m});
        } else {
            std::string mf = (fs::path(src.path) / "manifest.json").string();
            std::error_code ec2;
            if (!fs::exists(mf, ec2)) continue; // not a mod dir
            auto m = ttmod::parse_manifest(read_file(mf));
            out.push_back({m.identity.id, m.identity.version, src.name + "/", "", m});
        }
    }
    std::sort(out.begin(), out.end(), [](const Listed& a, const Listed& b) { return a.id < b.id; });
    return out;
}

ttmod::ModState load_state(const std::string& gamedir) {
    ttmod::ModState st;
    std::string p = (fs::path(gamedir) / "config" / "mods.json").string();
    auto f = ttmod::parse_state(read_file(p));
    if (f.ok) st = f.state;
    return st;
}
bool save_state(const std::string& gamedir, const ttmod::ModState& st) {
    std::error_code ec;
    fs::create_directories(fs::path(gamedir) / "config", ec);
    return write_file(((fs::path(gamedir) / "config" / "mods.json").string()), st.serialize() + "\n");
}

int cmd_detect(const char* exe) {
    ttmod::ExeInfo e = ttmod::parse_pe(exe);
    if (!e.ok) {
        std::printf("NOT-PE: %s\n", e.error.c_str());
        return 1;
    }
    ttmod::GameProfile p = ttmod::select_profile(e);
    std::printf("size: %llu\nmachine: 0x%04X (%s)\ntimestamp: %u\nfnv1a64: 0x%016llX\n"
                "profile: %s game=%s season=%d status=%s\n",
                (unsigned long long)e.file_size, e.machine, ttmod::arch_name(e.machine), e.timestamp,
                (unsigned long long)e.fnv1a64, p.id, p.game, p.season, ttmod::to_string(p.status));
    return 0;
}

void print_packinfo(const std::string& path) {
    auto v = ttmod::inspect_package(path);
    if (!v.ok) {
        std::printf("INVALID: %s\n", v.error.c_str());
        return;
    }
    auto m = ttmod::parse_manifest(v.manifest_text);
    std::printf("id: %s\nname: %s\nversion: %s\napi: %d\npackage_format: %d\ngames:",
                m.identity.id.c_str(), "(see manifest)", m.identity.version.c_str(), m.compat.api,
                m.package_format);
    for (auto& g : m.compat.games) std::printf(" %s", g.c_str());
    std::printf("\npriority: %d\nenabled-field: %s\nfiles: %u\nplugin: %s\narch: %s\nentries: %u\n",
                m.overrides.priority, m.enabled ? "true" : "false", (unsigned)m.overrides.files.size(),
                m.plugin.path.empty() ? "(none)" : m.plugin.path.c_str(),
                ttmod::to_string(m.compat.arch), (unsigned)v.files.size());
    bool native = !m.plugin.path.empty();
    for (auto& f : v.files)
        if (f.name.size() > 4 && f.name.compare(f.name.size() - 4, 4, ".dll") == 0) native = true;
    std::printf("contains native code: %s\n", native ? "YES - can execute arbitrary code" : "no");
    if (!m.deps.depends.empty()) {
        std::printf("depends:");
        for (auto& [id, ver] : m.deps.depends) std::printf(" %s%s%s", id.c_str(), ver.empty() ? "" : ">=", ver.c_str());
        std::printf("\n");
    }
    if (!m.deps.conflicts.empty()) {
        std::printf("conflicts:");
        for (auto& c : m.deps.conflicts) std::printf(" %s", c.c_str());
        std::printf("\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "detect" && argc == 3) return cmd_detect(argv[2]);
    if (cmd == "package" && argc >= 3) {
        std::string sub = argv[2];
        if (sub == "create" && argc == 5) {
            std::string err;
            if (!ttmod::create_package(argv[3], argv[4], err)) {
                std::printf("create failed: %s\n", err.c_str());
                return 1;
            }
            std::printf("created %s\n", argv[4]);
            return 0;
        }
        if (sub == "validate" && argc == 4) {
            auto v = ttmod::inspect_package(argv[3]);
            if (!v.ok) {
                std::printf("INVALID: %s\n", v.error.c_str());
                return 1;
            }
            auto m = ttmod::parse_manifest(v.manifest_text);
            std::printf("valid: %s %s (%u files)\n", m.identity.id.c_str(),
                        m.identity.version.c_str(), (unsigned)v.files.size());
            return 0;
        }
        if (sub == "info" && argc == 4) {
            print_packinfo(argv[3]);
            return 0;
        }
    }
    if (cmd == "inspect" && argc == 3) {
        auto h = ttmod::inspect_ttarch2(argv[2]);
        if (!h.ok) {
            std::printf("NOT-TTARCH2: %s\n", h.error.c_str());
            return 1;
        }
        std::printf("magic: %.4s\nsize: %llu\nu16: %u %u %u %u\n"
                    "u64: %llu %llu %llu\ntag: ",
                    h.magic, (unsigned long long)h.file_size, h.w[0], h.w[1], h.w[2], h.w[3],
                    (unsigned long long)h.q[0], (unsigned long long)h.q[1],
                    (unsigned long long)h.q[2]);
        for (int i = 0; i < 16; ++i) std::printf("%02x", h.tag[i]);
        std::printf("\n(note: field semantics Unknown; see docs)\n");
        return 0;
    }
    if (cmd == "map" && argc == 3) {
        auto m = ttmod::map_boot_log(read_file(argv[2]));
        if (!m.ok) {
            std::puts("cannot parse log");
            return 1;
        }
        std::printf("opens: %d\nby category:", m.total);
        for (auto& [k, v] : m.by_category) std::printf(" %s=%d", k.c_str(), v);
        std::printf("\nby ext:");
        for (auto& [k, v] : m.by_ext) std::printf(" %s=%d", k.c_str(), v);
        std::printf("\nresdesc files: %u\narchives: %u\nsaves: %u\n", (unsigned)m.resdesc_order.size(),
                (unsigned)m.archive_order.size(), (unsigned)m.save_paths.size());
        return 0;
    }
    if (cmd == "mods" && argc >= 4) {
        std::string sub = argv[2], dir = argv[3];
        if (sub == "list" && argc == 4) {
            auto mods = scan_mods_dir(dir);
            auto st = load_state(dir);
            if (mods.empty()) {
                std::puts("(no mods)");
                return 0;
            }
            for (auto& m : mods) {
                bool en = m.m.ok ? ttmod::effective_enabled(m.m, st)
                                 : st.enabled_for(m.id, true);
                std::printf("[%c] %-28s %-10s %s%s\n", en ? 'x' : ' ', m.id.c_str(), m.version.c_str(),
                        m.src.c_str(), m.m.ok ? "" : "  INVALID");
            }
            return 0;
        }
        if (sub == "info" && argc == 5) {
            for (auto& m : scan_mods_dir(dir)) {
                if (m.id != argv[4]) continue;
                auto st = load_state(dir);
                std::printf("id: %s\nversion: %s\nsource: %s\nenabled: %s\napi: %d\ngames:",
                        m.id.c_str(), m.version.c_str(), m.src.c_str(),
                        ttmod::effective_enabled(m.m, st) ? "true" : "false", m.m.compat.api);
                for (auto& g : m.m.compat.games) std::printf(" %s", g.c_str());
                std::printf("\nfiles: %u\n", (unsigned)m.m.overrides.files.size());
                return 0;
            }
            std::printf("unknown mod: %s\n", argv[4]);
            return 1;
        }
        if ((sub == "enable" || sub == "disable") && argc == 5) {
            bool found = false;
            for (auto& m : scan_mods_dir(dir))
                if (m.id == argv[4]) found = true;
            if (!found) {
                std::printf("unknown mod: %s\n", argv[4]);
                return 1;
            }
            auto st = load_state(dir);
            st.set(argv[4], sub == "enable");
            if (!save_state(dir, st)) {
                std::puts("cannot write config/mods.json");
                return 1;
            }
            std::printf("%s: %sd\n", argv[4], sub.c_str());
            return 0;
        }
    }
    return usage();
}
