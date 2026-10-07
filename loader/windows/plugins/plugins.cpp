// Plugin loader. Canonical discovery feeds it; depends/conflicts enforced
// enforced against the full present set. Failures are logged with reasons;
// a bad plugin never stops the game or other plugins. No unload (documented).
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "ttmod/log.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/plugin_api.h"
#include "events.hpp"
#include "menu_bridge.hpp"
#include "mods.hpp"
#include "modscan.hpp"
#include "plugins.hpp"
#include "win32_path.hpp"

namespace ttmod_win {
namespace {

static std::string g_logpath;

static void emit(const std::string& msg) {
    ttmod::Logger log;
    if (log.open(g_logpath)) log.info(msg);
}

static void host_log(const char* msg) {
    emit(msg ? msg : "(null)");
}

} // namespace

void plugins_init(const std::vector<ScannedMod>& all, const char* profile_id, const char* game, int season,
                  const char* log_path, ttmod::RuntimeOwner* owner) {
    g_logpath = log_path ? log_path : "";
    if (all.empty()) {
        emit("plugins: no mods discovered, skipping");
        return;
    }
    // `all` is already the surviving, dependency-ordered set produced by the
    // single ModPlan resolution in framework.cpp: blocked mods are excluded,
    // order is dependency-first. This loader therefore re-resolves nothing -
    // it loads what it is given, in the order it is given.
    // (It used to call resolve_dependencies() itself, which meant the
    // resource indexer and the plugin loader each decided independently
    // which mods were loadable.)

    // Static lifetime: plugins retain this pointer for async callbacks
    // (a stack instance would dangle after plugins_init returns). The
    // pointed-to STRINGS are static-owned too: callers pass c_str() views
    // into short-lived init state (Stage E: use-after-free proven by
    // inspection - InitCtx dies with the init thread).
    static ttmod_host host;
    static std::string owned_profile, owned_game;
    owned_profile = profile_id ? profile_id : "";
    owned_game = game ? game : "";
    host = ttmod_host{TTMOD_PLUGIN_API_VERSION,
                      owned_profile.c_str(),
                      owned_game.c_str(),
                      season,
                      host_log,
                      ttmod_win::events_subscribe,
                      ttmod_win::events_unsubscribe,
                      ttmod_win::events_get_state,
                      ttmod_win::mods_menu_count,
                      ttmod_win::mods_menu_info,
                      ttmod_win::menumods_queue_ui_chunk,
                      (uint32_t)sizeof(ttmod_host),
                      0};
    for (auto& s : all) {
        const ttmod::ModManifest& m = s.manifest;
        if (!s.has_dll) continue; // resource-only; mods loader owns it
        // M11: manifest-declared plugin path (or legacy plugin.dll), arch gate.
        if (!m.compat.supports_arch(ttmod::Architecture::X86)) {
            emit("plugins: " + m.identity.id.str() + " rejected (arch " + ttmod::to_string(m.compat.arch) + " != x86)");
            continue;
        }
        std::string dllpath = join(s.dir, s.plugin_rel);
        auto pe = ttmod::parse_pe(dllpath);
        if (!pe.ok() || pe.value().machine != 0x014C) {
            emit("plugins: " + m.identity.id.str() + " rejected (plugin is not x86 PE)");
            continue;
        }
        emit("plugins: " + m.identity.id.str() + " validated");
        emit("plugins: WARNING " + m.identity.id.str() +
             " contains native code and can execute arbitrary code (manifest-declared)");
        HMODULE dll = ttmod_win::load_library(dllpath);
        if (!dll) {
            char r[160];
            snprintf(r, sizeof r, "plugins: %s failed to load (err %lu)", m.identity.id.str().c_str(), GetLastError());
            emit(r);
            continue;
        }
        using InitFn = int (*)(const ttmod_host*);
        InitFn init = (InitFn)GetProcAddress(dll, "ttmod_plugin_init");
        if (!init) {
            emit("plugins: " + m.identity.id.str() + " has no ttmod_plugin_init, kept loaded (no init)");
            continue;
        }
        int rc = init(&host);
        char done[160];
        snprintf(done, sizeof done, "plugins: %s initialized (rc=%d)", m.identity.id.str().c_str(), rc);
        emit(done);
    }
}

} // namespace ttmod_win
#endif
