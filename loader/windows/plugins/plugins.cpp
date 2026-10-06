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
#include "ttmod/modgraph.hpp"
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

static void host_log(const char* msg) { emit(msg ? msg : "(null)"); }

} // namespace

void plugins_init(const std::vector<ScannedMod>& all, const char* profile_id, const char* game,
                  int season, const char* log_path) {
    g_logpath = log_path ? log_path : "";
    if (all.empty()) {
        emit("plugins: no mods discovered, skipping");
        return;
    }
    std::vector<ttmod::ModManifest> present;
    for (auto& s : all) present.push_back(s.manifest);
    ttmod::DepResolution dep = ttmod::resolve_dependencies(present);
    auto blocked_reason = [&](const std::string& id) -> const std::string* {
        for (auto& pr : dep.problems)
            if (pr.mod == id) return &pr.message;
        return nullptr;
    };

    // Static lifetime: plugins retain this pointer for async callbacks
    // (a stack instance would dangle after plugins_init returns). The
    // pointed-to STRINGS are static-owned too: callers pass c_str() views
    // into short-lived init state (Stage E: use-after-free proven by
    // inspection - InitCtx dies with the init thread).
    static ttmod_host host;
    static std::string owned_profile, owned_game;
    owned_profile = profile_id ? profile_id : "";
    owned_game = game ? game : "";
    host = ttmod_host{TTMOD_PLUGIN_API_VERSION, owned_profile.c_str(), owned_game.c_str(), season,
                      host_log,
                      ttmod_win::events_subscribe, ttmod_win::events_unsubscribe,
                      ttmod_win::events_get_state, ttmod_win::mods_menu_count,
                      ttmod_win::mods_menu_info, ttmod_win::menumods_queue_ui_chunk};
    for (auto& s : all) {
        const ttmod::ModManifest& m = s.manifest;
        if (const std::string* why = blocked_reason(m.identity.id)) {
            emit("plugins: " + m.identity.id + " rejected (" + *why + ")");
            continue;
        }
        if (!s.has_dll) continue; // resource-only; mods loader owns it
        // M11: manifest-declared plugin path (or legacy plugin.dll), arch gate.
        if (m.compat.arch != ttmod::Architecture::Any && m.compat.arch != ttmod::Architecture::X86) {
            emit("plugins: " + m.identity.id + " rejected (arch " + ttmod::to_string(m.compat.arch) + " != x86)");
            continue;
        }
        std::string dllpath = join(s.dir, s.plugin_rel);
        ttmod::ExeInfo pe = ttmod::parse_pe(dllpath);
        if (!pe.ok || pe.machine != 0x014C) {
            emit("plugins: " + m.identity.id + " rejected (plugin is not x86 PE)");
            continue;
        }
        emit("plugins: " + m.identity.id + " validated");
        emit("plugins: WARNING " + m.identity.id +
             " contains native code and can execute arbitrary code (manifest-declared)");
        HMODULE dll = LoadLibraryA(dllpath.c_str());
        if (!dll) {
            char r[160];
            snprintf(r, sizeof r, "plugins: %s failed to load (err %lu)", m.identity.id.c_str(), GetLastError());
            emit(r);
            continue;
        }
        using InitFn = int (*)(const ttmod_host*);
        InitFn init = (InitFn)GetProcAddress(dll, "ttmod_plugin_init");
        if (!init) {
            emit("plugins: " + m.identity.id + " has no ttmod_plugin_init, kept loaded (no init)");
            continue;
        }
        int rc = init(&host);
        char done[160];
        snprintf(done, sizeof done, "plugins: %s initialized (rc=%d)", m.identity.id.c_str(), rc);
        emit(done);
    }
}

} // namespace ttmod_win
#endif
