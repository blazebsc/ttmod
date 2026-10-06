// Mod index. Canonical discovery feeds it; depends/conflicts enforced;
// resolver built from surviving resource declarations.
#ifdef _WIN32
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "ttmod/log.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/modplan.hpp"
#include "ttmod/pathnorm.hpp"
#include "ttmod/resolver.hpp"
#include "mods.hpp"
#include "modscan.hpp"
#include "win32_path.hpp"

namespace ttmod_win {
namespace {

static std::string g_logpath;
static ttmod::Resolver g_resolver;
static bool g_verbose = false;

struct MenuEntry {
    std::string id, version;
    bool enabled = true, has_plugin = false, packaged = false;
    // Display/schema snapshot (from manifest at store time; internal only,
    // never exposed through the plugin ABI).
    std::string name, description;
    std::vector<ttmod::ConfigOption> schema;
    ttmod::ModManifest manifest;
};
static std::vector<MenuEntry> g_menu;
static std::mutex g_menu_mtx;

static void emit(const std::string& msg) {
    ttmod::Logger log;
    if (log.open(g_logpath)) log.info(msg);
}

} // namespace

void mods_init(const std::vector<ScannedMod>& all, const char* game_root, const char* log_path) {
    g_logpath = log_path ? log_path : "";
    g_verbose = GetEnvironmentVariableA("TTMOD_RESOLVE_VERBOSE", nullptr, 0) > 0;
    g_resolver.set_game_root(game_root ? game_root : "");
    // Dependencies/conflicts were resolved ONCE into the ModPlan (see
    // framework.cpp); `all` is already the surviving, load-ordered set.
    // This function only indexes resource overrides - it loads nothing and
    // re-decides nothing.
    for (auto& s : all) {
        const ttmod::ModManifest& m = s.manifest;
        if (m.overrides.files.empty()) continue; // native-only; plugins loader owns it
        ttmod::ModDef def{m.identity.id.str(), s.dir, m.overrides.priority, true, m.overrides.files};
        size_t before = g_resolver.problems().size();
        bool used = g_resolver.add_mod(def, exists);
        char sum[192];
        snprintf(sum, sizeof sum, "mods: %s indexed (priority %d, %s)", m.identity.id.str().c_str(),
                 m.overrides.priority, used ? "overrides registered" : "no usable overrides");
        emit(sum);
        for (size_t i = before; i < g_resolver.problems().size(); ++i)
            emit("mods: problem: " + g_resolver.problems()[i]);
    }
    char sum[160];
    snprintf(sum, sizeof sum, "mods: index ready (%u paths)", (unsigned)g_resolver.override_count());
    emit(sum);
}

void mods_store_menu(const std::vector<ScannedMod>& enabled, const std::vector<ttmod::Discovered>& disabled) {
    std::lock_guard<std::mutex> l(g_menu_mtx);
    g_menu.clear();
    for (auto& s : enabled)
        g_menu.push_back({s.manifest.identity.id.str(), s.manifest.identity.version, true, s.has_dll, s.packaged,
                          s.manifest.presentation.name, s.manifest.presentation.description,
                          s.manifest.presentation.config, s.manifest});
    for (auto& d : disabled)
        g_menu.push_back({d.id.str(), d.manifest.identity.version, false, !d.manifest.plugin.path.empty(), d.packaged,
                          d.manifest.presentation.name, d.manifest.presentation.description,
                          d.manifest.presentation.config, d.manifest});
    std::sort(g_menu.begin(), g_menu.end(), [](const MenuEntry& a, const MenuEntry& b) { return a.id < b.id; });
}

int mods_menu_count() {
    std::lock_guard<std::mutex> l(g_menu_mtx);
    return (int)g_menu.size();
}

int mods_menu_info(int index, ttmod_modinfo* out) {
    if (!out) return -1;
    std::lock_guard<std::mutex> l(g_menu_mtx);
    if (index < 0 || (size_t)index >= g_menu.size()) return -1;
    const MenuEntry& e = g_menu[(size_t)index];
    strncpy(out->id, e.id.c_str(), sizeof out->id - 1);
    out->id[sizeof out->id - 1] = '\0';
    strncpy(out->version, e.version.c_str(), sizeof out->version - 1);
    out->version[sizeof out->version - 1] = '\0';
    out->enabled = e.enabled ? 1 : 0;
    out->has_plugin = e.has_plugin ? 1 : 0;
    out->packaged = e.packaged ? 1 : 0;
    return 0;
}

bool mods_menu_manifest(int index, ttmod::ModManifest* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> l(g_menu_mtx);
    if (index < 0 || (size_t)index >= g_menu.size()) return false;
    *out = g_menu[(size_t)index].manifest;
    return true;
}

bool mods_resolve(const wchar_t* requested, wchar_t* out_path) {
    std::string req, replacement, winner;
    if (!mods_try(requested, req, replacement, winner)) return false;
    // Internal '/' -> Windows '\' for the real CreateFileW call.
    std::string win = to_win(replacement);
    return widen(win, out_path);
}

// Narrow-input core: resolve + diagnostics. out_requested echoes the input.
bool mods_try(const wchar_t* requested, std::string& out_requested, std::string& out_replacement,
              std::string& out_winner) {
    if (!requested) return false;
    out_requested = narrow(requested);
    ttmod::ResolveResult r = g_resolver.resolve(out_requested);
    if (g_verbose) {
        char m[768];
        snprintf(m, sizeof m, "resolve: found=%d reason=%s winner=%s shadowed=%u", (int)r.found, r.reason.c_str(),
                 r.winner.c_str(), (unsigned)r.shadowed.size());
        emit(m);
    }
    if (!r.found) return false;
    out_replacement = r.replacement;
    out_winner = r.winner;
    if (!r.shadowed.empty()) {
        std::string c = "RESOURCE CONFLICT path shadows:";
        for (auto& s : r.shadowed) c += " " + s;
        c += " | winner: " + r.winner + " — losers remain visible here, game uses winner";
        emit(c);
    }
    char m[768];
    snprintf(m, sizeof m, "override: %s -> [%s] req=[%s] %s", r.winner.c_str(), r.replacement.c_str(),
             out_requested.c_str(), r.shadowed.empty() ? "" : "(conflict, see above)");
    emit(m);
    return true;
}

} // namespace ttmod_win
#endif
