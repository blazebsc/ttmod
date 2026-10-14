// hello-mcsm example plugin (M3): proves discovery → validation → load → init.
// v6: also registers ttmod.native["hello.mcsm"].ping (PR 34 demo).
#include "ttmod/plugin_api.h"
#include <cstdio>

static int ping_cb(void*, const struct ttmod_value* args, int nargs, struct ttmod_value* ret, char* err,
                   size_t err_len) {
    if (nargs < 1 || args[0].type != TT_STRING || !args[0].s) {
        snprintf(err, err_len, "ping needs one string");
        return 1;
    }
    // Bracket syntax: the id's dot breaks dot-syntax. ret strings are
    // copied by the host during the call, so the arg borrow is safe.
    ret->type = TT_STRING;
    ret->s = args[0].s;
    return 0;
}

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 1) return -2;
    char msg[256];
    snprintf(msg, sizeof msg, "hello-mcsm: hello from %s (profile %s, api %d)",
             host->game, host->profile_id, host->api_version);
    host->log(msg);
    // PR 34: register only where the host has it (v6+); older hosts run
    // the hello path above untouched (this is the >= + null-check idiom
    // every plugin should use for newer fields).
    if (host->api_version >= 6 && host->register_api) {
        int tok = host->register_api("hello", "ping", ping_cb, nullptr, 1, 1);
        snprintf(msg, sizeof msg, "hello-mcsm: ping binding %s (token %d)",
                 tok > 0 ? "registered" : "refused", tok);
        host->log(msg);
    }
    return 0;
}
