// hello-mcsm example plugin (M3): proves discovery → validation → load → init.
#include "ttmod/plugin_api.h"
#include <cstdio>

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version != TTMOD_PLUGIN_API_VERSION) return -2;
    char msg[256];
    snprintf(msg, sizeof msg, "hello-mcsm: hello from %s (profile %s, api %d)",
             host->game, host->profile_id, host->api_version);
    host->log(msg);
    return 0;
}
