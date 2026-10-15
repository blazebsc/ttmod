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
#include "ttmod/plugin_bind.hpp"
#include "ttmod/diag.hpp"
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

// PR 24: structured plugin events (diag|plugin|<code>|<id>|<detail>).
// Codes reuse the historical words so old greps (rejected, initialized,
// failed, grants) keep hitting.
static void diag(const char* code, const std::string& id, const std::string& detail) {
    ttmod::DiagEvent e{ttmod::DiagKind::Plugin, code, id, detail};
    emit(ttmod::diag_format(e));
}

static void host_log(const char* msg) {
    emit(msg ? msg : "(null)");
}

// PR 21/34 lifetime+attribution: the host struct is built PER PLUGIN
// (stable addresses in a deque - plugins retain the pointer), differing
// only in subscribe/register_api, which attribute to the plugin currently
// initializing. The loader initializes sequentially on one thread, so the
// current-record pointer needs no locking; plugins must subscribe and
// register from the init thread (init must be non-blocking anyway).
// Post-init calls still work but are unattributed (no record to charge;
// failed-init revocation cannot see them) - documented in native-abi.md.
static ttmod::PluginRecord* g_current_plugin = nullptr;
static ttmod::RuntimeOwner* g_owner = nullptr;
static ttmod::ScriptVm* g_luau_vm = nullptr; // PR 31 wiring; null = none

static int host_subscribe(int event_id, ttmod_event_cb cb, void* ctx) {
    int tok = ttmod_win::events_subscribe(event_id, cb, ctx);
    if (g_current_plugin != nullptr) ttmod::plugin_note_subscribed(*g_current_plugin, tok);
    return tok;
}
// PR 34: native script bindings. Registers through the shared registry
// (which forces the native/<id> namespace, rejects dupes and bad arity)
// and installs on the live VM when there is one (Null accepts, real
// backend trampolines). Returns the slot token, or -1.
static int host_register_api(const char* ns, const char* name, ttmod_native_cb cb, void* ctx, int min_args,
                             int max_args) {
    if (!g_owner || !g_current_plugin || !ns || !name || !cb) return -1;
    const std::string id = g_current_plugin->id;
    ttmod::ApiKey key;
    key.ns = ns;
    key.name = name;
    auto tok = g_owner->api_registry().register_fn({ttmod::ApiOwnerKind::Plugin, id},
                                                   key, ttmod::make_plugin_binding(cb, ctx),
                                                   (uint32_t)ttmod::kApiVersion, min_args, max_args);
    if (!tok.ok()) {
        diag("bind-failed", id, key.dotted() + ": " + tok.error().message);
        return -1;
    }
    // Install on both live VMs (PR 31): Lua first (must succeed), then
    // Luau when present - same key, same token, both trampolines dispatch
    // through the one registry entry.
    if (ttmod::ScriptVm* vm = g_owner->vm()) {
        ttmod::ApiOwner owner{ttmod::ApiOwnerKind::Plugin, id};
        ttmod::ApiKey effective{owner.effective_ns(), key.name};
        if (!vm->install_api(tok.value(), effective, (uint32_t)ttmod::kApiVersion).ok()) {
            diag("bind-install-failed", id, key.dotted());
            return -1;
        }
        if (g_luau_vm != nullptr &&
            !g_luau_vm->install_api(tok.value(), effective, (uint32_t)ttmod::kApiVersion).ok()) {
            diag("bind-install-failed", id, key.dotted() + " (luau)");
            return -1;
        }
    }
    return (int)tok.value();
}

} // namespace

void plugins_init(const std::vector<ScannedMod>& all, const char* profile_id, const char* game, int season,
                  const char* log_path, ttmod::RuntimeOwner* owner) {
    g_logpath = log_path ? log_path : "";
    if (all.empty()) {
        diag("no-mods", "", "no mods discovered, skipping");
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
                            0,
                            host_register_api};
    g_owner = owner;
    for (auto& s : all) {
        const ttmod::ModManifest& m = s.manifest;
        if (!s.has_dll) continue; // resource-only; mods loader owns it
        // PR 20 capability gate: a manifest whose non-empty runtimes disclaim
        // Native while shipping a DLL is contradictory - refuse with a reason
        // instead of executing code the author said isn't there. Empty
        // runtimes = legacy/unspecified = allowed (all shipped mods).
        if (!ttmod::plugin_runtime_allowed(m.runtime.runtimes)) {
            diag("rejected-no-native-runtime", m.identity.id.str(), "runtimes declare no native runtime");
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
            diag("grants", m.identity.id.str(), g[0] ? g : "(none requested)");
        }
        // M11: manifest-declared plugin path (or legacy plugin.dll), arch gate.
        if (!m.compat.supports_arch(ttmod::Architecture::X86)) {
            char r[96];
            snprintf(r, sizeof r, "arch %s != x86", ttmod::to_string(m.compat.arch));
            diag("rejected-arch", m.identity.id.str(), r);
            continue;
        }
        std::string dllpath = join(s.dir, s.plugin_rel);
        auto pe = ttmod::parse_pe(dllpath);
        if (!pe.ok() || pe.value().machine != 0x014C) {
            diag("rejected-not-pe", m.identity.id.str(), "plugin is not x86 PE");
            continue;
        }
        diag("validated", m.identity.id.str(), "");
        diag("native-warning", m.identity.id.str(),
             "contains native code and can execute arbitrary code (manifest-declared)");
        HMODULE dll = ttmod_win::load_library(dllpath);
        if (!dll) {
            char r[160];
            snprintf(r, sizeof r, "err %lu", GetLastError());
            diag("load-failed", m.identity.id.str(), r);
            continue;
        }
        using InitFn = int (*)(const ttmod_host*);
        InitFn init = (InitFn)GetProcAddress(dll, "ttmod_plugin_init");
        if (!init) {
            diag("no-export", m.identity.id.str(), "kept loaded (no init)");
            continue;
        }
        // PR 21 lifetime: per-plugin record + host copy; a nonzero rc
        // revokes that plugin's subscriptions synchronously, before the
        // next plugin initializes.
        ttmod::PluginRecord rec;
        rec.id = m.identity.id.str();
        rec.state = ttmod::PluginState::Loaded;
        hosts.push_back(shared);
        g_current_plugin = &rec;
        int rc = init(&hosts.back());
        g_current_plugin = nullptr;
        auto revoke = ttmod::plugin_note_init_result(rec, rc);
        for (int t : revoke) ttmod_win::events_unsubscribe(t);
        char done[192];
        if (revoke.empty())
            snprintf(done, sizeof done, "rc=%d", rc);
        else
            snprintf(done, sizeof done, "rc=%d, revoked %u callbacks", rc, (unsigned)revoke.size());
        diag(revoke.empty() ? "initialized" : "failed-revoked", m.identity.id.str(), done);
    }
}

// Outside the anonymous namespace: declared in plugins.hpp (PR 31 wiring).
void plugins_set_luau_vm(ttmod::ScriptVm* vm) {
    g_luau_vm = vm;
}

} // namespace ttmod_win
#endif
