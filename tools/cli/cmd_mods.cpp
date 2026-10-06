#include "commands.hpp"

#include <cstdio>
#include <string>

#include "common.hpp"
#include "ttmod/modstate.hpp"

int cmd_mods(int argc, char** argv) {
    if (argc < 2) return cmd_usage();
    std::string sub = argv[0], dir = argv[1];
    if (sub == "list" && argc == 2) {
        auto mods = scan_mods_dir(dir);
        auto st = load_state(dir);
        if (mods.empty()) {
            std::puts("(no mods)");
            return 0;
        }
        for (auto& m : mods) {
            bool en = m.m.ok ? ttmod::effective_enabled(m.m, st) : st.enabled_for(m.id, true);
            std::printf("[%c] %-28s %-10s %s%s\n", en ? 'x' : ' ', m.id.c_str(), m.version.c_str(),
                    m.src.c_str(), m.m.ok ? "" : "  INVALID");
        }
        return 0;
    }
    if (sub == "info" && argc == 3) {
        for (auto& m : scan_mods_dir(dir)) {
            if (m.id != argv[2]) continue;
            auto st = load_state(dir);
            std::printf("id: %s\nversion: %s\nsource: %s\nenabled: %s\napi: %d\ngames:",
                    m.id.c_str(), m.version.c_str(), m.src.c_str(),
                    ttmod::effective_enabled(m.m, st) ? "true" : "false", m.m.compat.api);
            for (auto& g : m.m.compat.games) std::printf(" %s", g.c_str());
            std::printf("\nfiles: %u\n", (unsigned)m.m.overrides.files.size());
            return 0;
        }
        std::printf("unknown mod: %s\n", argv[2]);
        return 1;
    }
    if ((sub == "enable" || sub == "disable") && argc == 3) {
        bool found = false;
        for (auto& m : scan_mods_dir(dir))
            if (m.id == argv[2]) found = true;
        if (!found) {
            std::printf("unknown mod: %s\n", argv[2]);
            return 1;
        }
        auto st = load_state(dir);
        st.set(argv[2], sub == "enable");
        if (!save_state(dir, st)) {
            std::puts("cannot write config/mods.json");
            return 1;
        }
        std::printf("%s: %sd\n", argv[2], sub.c_str());
        return 0;
    }
    return cmd_usage();
}
