// modmenu-core backend (Milestone A): lists installed mods via host ABI v4
// (get_mod_count/get_mod_info) and logs them. This is the data source the
// future native Mods screen will render; today the log IS the screen.
// Per-mod config surface: each mod owns config/<id>.json (see demo-config);
// enable/disable stays in config/mods.json (framework-owned).
#include "ttmod/plugin_api.h"
#include <cstdio>

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 4 || !host->get_mod_count || !host->get_mod_info) {
        host->log("modmenu-core: needs host api >= 4 (mod listing), staying unloaded");
        return -2;
    }
    int n = host->get_mod_count();
    char m[128];
    snprintf(m, sizeof m, "modmenu-core: %d mod(s) installed", n < 0 ? 0 : n);
    host->log(m);
    for (int i = 0; i < n && i < 64; ++i) {
        ttmod_modinfo info{};
        if (host->get_mod_info(i, &info) != 0) break;
        char e[256];
        snprintf(e, sizeof e, "modmenu-core: [%d] %s %s %s%s%s", i, info.id, info.version,
                 info.enabled ? "enabled" : "disabled", info.has_plugin ? " +plugin" : "",
                 info.packaged ? " +packed" : "");
        host->log(e);
    }
    if (n > 64) host->log("modmenu-core: ... (truncated, see config/mods.json for full state)");
    return 0;
}
