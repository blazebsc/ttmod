// See menu_bridge.hpp. Snapshot + setters over core primitives; thin
// Win32 file I/O; same-thread Lua C functions with strict stack balance.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ttmod/log.hpp"
#include "win32_path.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/modconfig.hpp"
#include "ttmod/validate.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/plugin_api.h"
#include "ttmod/uiqueue.hpp"
#include "lua_abi.hpp"
#include "lua_bridge.hpp"
#include "menu_bridge.hpp"
#include "mods.hpp"
#include "menumods_ui.h" // generated from menumods_ui.lua (not committed)

namespace ttmod_win {
namespace {

static std::string g_logpath;
static std::string g_gamedir;
static volatile LONG g_seq = 0;

static LuaLoadstringFn g_loadstring = nullptr;
static LuaPcallkFn g_pcallk = nullptr;
static LuaGettopFn g_gettop = nullptr;
static LuaTolstringFn g_tolstring = nullptr;
static LuaSetglobalFn g_setglobal = nullptr;

static void emit(const std::string& msg) {
    ttmod::Logger log;
    if (log.open(g_logpath)) log.info(msg);
}

static std::string read_file(const std::string& path) {
    FILE* f = ttmod::file_io::open_read(path);
    if (!f) return "";
    std::string t;
    char b[1024];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
    fclose(f);
    return t;
}

// Atomic write of a per-mod config file. Delegates to the shared helper so
// the unique-temp + flush + rename policy exists in exactly one place.
static bool write_file(const std::string& path, const std::string& text) {
    return ttmod::file_io::write_file_atomic(path, text);
}

// Fresh snapshot: manifests (store time) + enabled (fresh mods.json) +
// values (fresh per-mod files). Missing files -> defaults.
static std::vector<ttmod::MenuModSnapshot> snapshot() {
    std::vector<ttmod::MenuModSnapshot> out;
    ttmod::ModState st;
    auto sf = ttmod::parse_state(read_file(g_gamedir + "\\config\\mods.json"));
    if (sf.ok()) st = sf.value();
    int n = mods_menu_count();
    for (int i = 0; i < n; ++i) {
        ttmod::ModManifest m;
        if (!mods_menu_manifest(i, &m)) continue;
        ttmod::MenuModSnapshot s;
        s.id = m.identity.id.str();
        s.name = m.presentation.name;
        s.version = m.identity.version.str();
        s.description = m.presentation.description;
        s.enabled = ttmod::effective_enabled(m, st);
        ttmod_modinfo mi{};
        if (mods_menu_info(i, &mi) == 0) {
            s.has_plugin = mi.has_plugin != 0;
            s.packaged = mi.packaged != 0;
        }
        s.schema = m.presentation.config;
        // A missing or corrupt config file is not fatal: fall back to
        // schema defaults (an empty ConfigFile means "no overrides").
        auto cf = ttmod::parse_config_file(read_file(g_gamedir + "\\config\\" + m.identity.id.str() + ".json"));
        s.values = ttmod::config_effective(m.presentation.config, cf.ok() ? cf.value() : ttmod::ConfigFile{});
        out.push_back(s);
    }
    return out;
}

} // namespace

// Read the error object a failed pcallk left on the stack, then restore the
// stack. pcall's return code alone ("2" = a value error) names no cause; the
// message is the only usable diagnostic and click-path errors are swallowed.
static const char* take_error(lua_State* L, LuaGettopFn gettop, LuaTolstringFn tolstring) {
    if (!tolstring || !gettop) return nullptr;
    int top = gettop(L);
    const char* msg = top >= 1 ? tolstring(L, -1, nullptr) : nullptr;
    static thread_local char buf[512];
    snprintf(buf, sizeof buf, "%s", msg ? msg : "?");
    return buf; // caller restores the stack (it owns the pop policy)
}

void bridge_run_chunk(lua_State* L, LuaLoadstringFn loadstring, LuaPcallkFn pcallk, LuaGettopFn gettop,
                      LuaSetglobalFn setglobal, LuaTolstringFn tolstring, const char* what, const char* chunk) {
    if (!L || !loadstring || !pcallk || !gettop || !chunk) return;
    int t0 = gettop(L);
    int lr = loadstring(L, chunk);
    int pr = -1;
    const char* err = nullptr;
    if (lr == 0) {
        pr = pcallk(L, 0, 0, 0, 0, nullptr);
        if (pr != 0) {
            err = take_error(L, gettop, tolstring);
            // balance first, before any other call can touch the error object
            if (setglobal) setglobal(L, "ttmod_last_error");
        }
    }
    int t1 = gettop(L);
    if (t1 != t0 && setglobal) {
        setglobal(L, "ttmod_last_error"); // sink stray leftovers, keep balance
        t1 = gettop(L);
    }
    if (lr != 0 || pr != 0 || t1 != t0) {
        char m[640];
        snprintf(m, sizeof m, "lua: %s chunk load=%d pcall=%d balanced=%d err=%s", what ? what : "?", lr, pr, t1 == t0,
                 err ? err : "-");
        emit(m);
    }
}

// ttmod_menu_refresh(): rebuild the ttmod_menu literal on this state.
static int __cdecl fn_refresh(lua_State* L) {
    if (!g_loadstring || !g_pcallk || !g_gettop) return 0;
    unsigned seq = (unsigned)InterlockedIncrement(&g_seq);
    auto snap = snapshot();
    std::string chunk = ttmod::build_menu_literal(snap, seq);
    bridge_run_chunk(L, g_loadstring, g_pcallk, g_gettop, g_setglobal, g_tolstring, "refresh", chunk.c_str());
    char m[128]; // menu opens are rare; one line each is fine
    snprintf(m, sizeof m, "menumods: refresh seq=%u mods=%u", seq, (unsigned)snap.size());
    emit(m);
    return 0; // no results; stack exactly as entered
}

static int __cdecl fn_set_enabled(lua_State* L) {
    const char* id = g_tolstring ? g_tolstring(L, 1, nullptr) : nullptr;
    const char* v = g_tolstring ? g_tolstring(L, 2, nullptr) : nullptr;
    if (!id || !id[0] || !v) return 0;
    bool en = v[0] == '1';
    std::string path = g_gamedir + "\\config\\mods.json";
    auto next = ttmod::apply_enabled_change(read_file(path), id, en);
    if (!next.ok()) {
        emit(std::string("menumods: enabled write REFUSED for ") + id + ": " + next.error().message);
        return 0;
    }
    if (!write_file(path, next.value())) {
        emit(std::string("menumods: enabled write FAILED for ") + id);
        return 0;
    }
    char m[192];
    snprintf(m, sizeof m, "menumods: %s %s (restart applies)", id, en ? "enabled" : "disabled");
    emit(m);
    return 0;
}

static int __cdecl fn_set_value(lua_State* L) {
    const char* id = g_tolstring ? g_tolstring(L, 1, nullptr) : nullptr;
    const char* key = g_tolstring ? g_tolstring(L, 2, nullptr) : nullptr;
    const char* val = g_tolstring ? g_tolstring(L, 3, nullptr) : nullptr;
    if (!id || !id[0] || !key || !key[0] || !val) return 0;
    // Trust boundary (Lua -> filesystem): the id becomes a filename.
    // Manifest ids are validated at parse, but this is the write path.
    if (!ttmod::is_valid_mod_id(id)) {
        emit(std::string("menumods: rejecting bad id for config write"));
        return 0;
    }
    ttmod::ModManifest m;
    int n = mods_menu_count();
    bool found = false;
    for (int i = 0; i < n; ++i) {
        if (mods_menu_manifest(i, &m) && m.identity.id.str() == id) {
            found = true;
            break;
        }
    }
    if (!found || m.presentation.config.empty()) return 0;
    std::string path = g_gamedir + "\\config\\" + std::string(id) + ".json";
    auto r = ttmod::apply_config_value(m.presentation.config, read_file(path), key, val);
    if (!r.ok()) {
        char msg[256];
        snprintf(msg, sizeof msg, "menumods: value rejected %s.%s", id, key);
        emit(msg);
        return 0;
    }
    if (!write_file(path, r.value())) {
        emit(std::string("menumods: config write FAILED for ") + id);
        return 0;
    }
    return 0;
}

// ttmod_menu_log(s): Lua-side UI trace. The engine runs click callbacks via
// DoString and swallows errors silently — without this, a nil engine global
// inside a screen build presents as "button does nothing" with zero evidence.
static int __cdecl fn_log(lua_State* L) {
    const char* s = g_tolstring ? g_tolstring(L, 1, nullptr) : nullptr;
    emit(std::string("menumods-lua: ") + (s ? s : "?"));
    return 0;
}

// ttmod_menu_palette("1"/"0"): the colour picker tells native code it is the
// active screen. The engine's hover GLOW re-applies its global light colour
// (a static struct, see kScolLightStructRva) to whichever row is selected,
// washing out any per-row colour - proven by decompiling the two flood call
// sites (they write 'Light Color Diffuse'/'Light Color Specular' from that
// static). While the picker is up, the scol hook redirects those writes to a
// black copy so the swatch colours stay pure; every other screen keeps the
// accent glow untouched. Set on push, cleared by the Menu_Pop wrapper and by
// every other screen builder in our chunk (belt and braces against a missed
// pop).
static volatile LONG g_palette_open = 0;
static int __cdecl fn_palette(lua_State* L) {
    const char* s = g_tolstring ? g_tolstring(L, 1, nullptr) : nullptr;
    // Phases: '1' = picker build (white register: identity creation light),
    // '2' = picker built (accent register: the unhover restore reads it and
    //       rewrites accent onto the accent-painted rows - invisible),
    // '0' = closed (accent register, gate off).
    // The register (kScolLightStructRva) is copied into every material at
    // creation, pushed by the styling writes, and READ BY THE UNHOVER
    // RESTORE as the colour a deselected row reverts to - one global, by
    // design. Phasing it white->accent around the build is the only stable
    // control we have; per-row restore values are impossible.
    int phase = (s && s[0] == '1') ? 1 : ((s && s[0] == '2') ? 2 : 0);
    LONG flag = (phase != 0) ? 1 : 0;
    InterlockedExchange(&g_palette_open, flag);
    ttmod_win::lua_bridge_set_light_palette(phase == 1);
    if (phase == 1) emit("palette: register white (picker building)");
    else if (phase == 2) emit("palette: register accent (picker built)");
    else emit("palette: register accent (closed)");
    return 0;
}

void menumods_set_palette_open(bool open) {
    InterlockedExchange(&g_palette_open, open ? 1 : 0);
}

bool menumods_palette_open() {
    return g_palette_open != 0;
}

static void reg_fn(lua_State* L, LuaPushCClosureFn pushcclosure, LuaSetglobalFn setglobal, lua_CFunction fn,
                   const char* name) {
    if (!pushcclosure || !setglobal) return;
    pushcclosure(L, fn, 0);
    setglobal(L, name);
}

void menumods_init(const char* game_dir, const char* log_path, ttmod::RuntimeOwner* owner) {
    g_gamedir = game_dir ? game_dir : "";
    g_logpath = log_path ? log_path : "";
}

int menumods_queue_ui_chunk(const char* code) {
    if (!code || !code[0] || !ttmod::uiqueue_push(code)) {
        emit("menumods: ui chunk dropped (queue full or empty)");
        return -1;
    }
    return 0;
}

bool menumods_button_enabled() {
    static bool checked = false;
    static bool enabled = true;
    if (!checked) {
        checked = true;
        char env[8] = {};
        if (GetEnvironmentVariableA("TTMOD_MENU", env, sizeof env) > 0 && strcmp(env, "0") == 0) {
            enabled = false;
            emit("menumods: menu button disabled via TTMOD_MENU=0");
        } else if (ttmod_win::exists(g_gamedir + "/config/menu-disabled")) {
            enabled = false;
            emit("menumods: menu button disabled via config/menu-disabled");
        }
    }
    return enabled;
}

void menumods_register(lua_State* L, LuaLoadstringFn loadstring, LuaPcallkFn pcallk, LuaGettopFn gettop,
                       LuaTolstringFn tolstring, LuaPushCClosureFn pushcclosure, LuaSetglobalFn setglobal) {
    if (!L || !loadstring || !pcallk || !gettop || !tolstring || !pushcclosure || !setglobal) return;
    g_loadstring = loadstring;
    g_pcallk = pcallk;
    g_gettop = gettop;
    g_tolstring = tolstring;
    g_setglobal = setglobal;
    reg_fn(L, pushcclosure, setglobal, fn_refresh, "ttmod_menu_refresh");
    reg_fn(L, pushcclosure, setglobal, fn_set_enabled, "ttmod_menu_set_enabled");
    reg_fn(L, pushcclosure, setglobal, fn_set_value, "ttmod_menu_set_value");
    reg_fn(L, pushcclosure, setglobal, fn_log, "ttmod_menu_log");
    reg_fn(L, pushcclosure, setglobal, fn_palette, "ttmod_menu_palette");
    // Replay plugin chunks ON THE MENU STATE. The LoadResource drain already ran
    // them, but on the engine state (_engine.lua et al) - a different lua_State.
    // Globals a plugin sets (TTMOD_ACCENT, ...) therefore never reach the state
    // menumods_ui.lua runs in, so the menu could never be themed. Queued chunks
    // are already drained, so replay from the retained queue snapshot instead.
    for (const std::string& c : ttmod::uiqueue_recent())
        bridge_run_chunk(L, loadstring, pcallk, gettop, setglobal, tolstring, "menu-plugin", c.c_str());
    // Colour-property probe: TTMOD_PROBE=1 (env) or config/probe-props makes the
    // UI dump every candidate AgentSetProperty name against a real label clone
    // and log which one reads back. Diagnostic only, logged, off by default.
    {
        char probe[8] = {};
        bool on = GetEnvironmentVariableA("TTMOD_PROBE", probe, sizeof probe) > 0 && strcmp(probe, "0") != 0;
        if (!on && ttmod_win::exists(g_gamedir + "/config/probe-props")) {
            on = true;
        }
        if (on) {
            emit("menumods: probe mode on, will dump colour properties to the log");
            bridge_run_chunk(L, g_loadstring, g_pcallk, g_gettop, g_setglobal, g_tolstring, "probe-enable",
                             "TTMOD_PROBE_PROPS = 1");
        }
    }
    bridge_run_chunk(L, g_loadstring, g_pcallk, g_gettop, g_setglobal, g_tolstring, "ui-defs", kMenuModsUi);
    emit("menumods: ui registered on state");
}

} // namespace ttmod_win
#endif
