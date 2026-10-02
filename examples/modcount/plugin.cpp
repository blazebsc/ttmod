// modcount example plugin: reports installed/enabled mod count at init via
// the stable v4 listing (get_mod_count/get_mod_info). No events, no hooks.
#include "ttmod/plugin_api.h"
#include <cstdio>

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 4 || !host->get_mod_count || !host->get_mod_info) {
        host->log("modcount: needs host api >= 4, staying unloaded");
        return -2;
    }
    int total = host->get_mod_count();
    if (total < 0) {
        host->log("modcount: mod listing unavailable");
        return -3;
    }
    int enabled = 0;
    for (int i = 0; i < total; ++i) {
        ttmod_modinfo mi{};
        if (host->get_mod_info(i, &mi) == 0 && mi.enabled) ++enabled;
    }
    char m[128];
    snprintf(m, sizeof m, "modcount: %d of %d mods enabled", enabled, total);
    host->log(m);
    return 0;
}
