// Plugin loader. Canonical discovery feeds it; depends/conflicts enforced
// enforced against the full present set. Failures are logged with reasons;
// a bad plugin never stops the game or other plugins. No unload (documented).
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "ttmod/log.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/plugin_api.h"
#include "ttmod/plugin_grant.hpp"
#include "ttmod/plugin_lifecycle.hpp"
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

// PR 21 lifetime: the host struct is built PER PLUGIN (stable addresses
// in a deque - plugins retain the pointer), differing only in subscribe,
// which records granted tokens into the plugin currently initializing so
// a failed init can revoke them. The loader initializes sequentially on
// one thread, so the current-record pointer needs no locking; plugins
// must subscribe from the init thread (init must be non-blocking
// anyway). The profile/game STRINGS stay shared - they never differ.
static ttmod::PluginRecord* g_subscribing = nullptr;
static int host_subscribe(int event_id, ttmod_event_cb cb, void* ctx) {
    int tok = ttmod_win::events_subscribe(event_id, cb, ctx);
    if (g_subscribing != nullptr) ttmod::plugin_note_subscribed(*g_subscribing, tok);
    return tok;
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
    static std::deque<ttmod_host> hosts; // one per initialized plugin; deque keeps addresses stable
    owned_profile = profile_id ? profile_id : "";
    owned_game = game ? game : "";
    // Never cleared: plugins retain these pointers, and a second init pass
    // must not dangle the first pass's hosts (append-only, like the ABI).
    const ttmod_host shared{TTMOD_PLUGIN_API_VERSION,
                            owned_profile.c_str(),
                            owned_game.c_str(),
                            season,
                            host_log,
                            host_subscribe,
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
        // PR 20 capability gate: a manifest whose non-empty runtimes disclaim
        // Native while shipping a DLL is contradictory - refuse with a reason
        // instead of executing code the author said isn't there. Empty
        // runtimes = legacy/unspecified = allowed (all shipped mods).
        if (!ttmod::plugin_runtime_allowed(m.runtime.runtimes)) {
            emit("plugins: " + m.identity.id.str() + " rejected (runtimes declare no native runtime)");
            continue;
        }
        // PR 20 negotiation log: granted subset of the manifest's requested
        // permissions (host-API-backed only; LoadLibrary is still arbitrary
        // code - see the WARNING below, unchanged). snprintf style like the
        // surrounding emits: its literals provably survive in this TU.
        {
            char g[256];
            size_t n = 0;
            g[0] = '\0';
            for (auto p : ttmod::plugin_grants(profile_id, m.runtime.permissions)) {
                const char* nm = ttmod::to_string(p);
                size_t len = strlen(nm);
                if (n > 0 && n + 1 < sizeof g) {
                    g[n++] = ',';
                    g[n] = '\0';
                }
                if (n + len < sizeof g) {
                    memcpy(g + n, nm, len + 1);
                    n += len;
                }
            }
            char line[384];
            snprintf(line, sizeof line, "plugins: %s grants: %s", m.identity.id.str().c_str(),
                     g[0] ? g : "(none requested)");
            emit(line);
        }
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
        // PR 21 lifetime: per-plugin record + host copy; a nonzero rc
        // revokes that plugin's subscriptions synchronously, before the
        // next plugin initializes.
        ttmod::PluginRecord rec;
        rec.id = m.identity.id.str();
        rec.state = ttmod::PluginState::Loaded;
        hosts.push_back(shared);
        g_subscribing = &rec;
        int rc = init(&hosts.back());
        g_subscribing = nullptr;
        auto revoke = ttmod::plugin_note_init_result(rec, rc);
        for (int t : revoke) ttmod_win::events_unsubscribe(t);
        char done[192];
        if (revoke.empty())
            snprintf(done, sizeof done, "plugins: %s initialized (rc=%d)", m.identity.id.str().c_str(), rc);
        else
            snprintf(done, sizeof done, "plugins: %s failed (rc=%d, revoked %u callbacks)",
                     m.identity.id.str().c_str(), rc, (unsigned)revoke.size());
        emit(done);
    }
}

} // namespace ttmod_win
#endif
