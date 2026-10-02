// See menu_bridge.hpp. Snapshot + setters over core primitives; thin
// Win32 file I/O; same-thread Lua C functions with strict stack balance.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ttmod/log.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/modconfig.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/plugin_api.h"
#include "ttmod/uiqueue.hpp"
#include "lua_abi.hpp"
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
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string t;
    char b[1024];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
    fclose(f);
    return t;
}

static bool write_file(const std::string& path, const std::string& text) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t w = fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return w == text.size();
}

// Fresh snapshot: manifests (store time) + enabled (fresh mods.json) +
// values (fresh per-mod files). Missing files -> defaults.
static std::vector<ttmod::MenuModSnapshot> snapshot() {
    std::vector<ttmod::MenuModSnapshot> out;
    ttmod::ModState st;
    ttmod::StateFile sf = ttmod::parse_state(read_file(g_gamedir + "\\config\\mods.json"));
    if (sf.ok) st = sf.state;
    int n = mods_menu_count();
    for (int i = 0; i < n; ++i) {
        ttmod::ModManifest m;
        if (!mods_menu_manifest(i, &m)) continue;
        ttmod::MenuModSnapshot s;
        s.id = m.id;
        s.name = m.name;
        s.version = m.version;
        s.description = m.description;
        s.enabled = st.enabled_for(m.id, m.enabled);
        ttmod_modinfo mi{};
        if (mods_menu_info(i, &mi) == 0) {
            s.has_plugin = mi.has_plugin != 0;
            s.packaged = mi.packaged != 0;
        }
        s.schema = m.config;
        s.values = ttmod::config_effective(
            m.config, ttmod::parse_config_file(read_file(g_gamedir + "\\config\\" + m.id + ".json")));
        out.push_back(s);
    }
    return out;
}

static void run_chunk(lua_State* L, const char* what, const char* chunk) {
    if (!g_loadstring || !g_pcallk || !g_gettop) return;
    int t0 = g_gettop(L);
    int lr = g_loadstring(L, chunk);
    int pr = -1;
    if (lr == 0) pr = g_pcallk(L, 0, 0, 0, 0, nullptr);
    int t1 = g_gettop(L);
    if (t1 != t0 && g_setglobal) {
        g_setglobal(L, "ttmod_last_error"); // sink stray error, keep balance
        t1 = g_gettop(L);
    }
    if (lr != 0 || pr != 0 || t1 != t0) {
        char m[160];
        snprintf(m, sizeof m, "menumods: %s FAILED load=%d pcall=%d", what, lr, pr);
        emit(m);
    }
}

// ttmod_menu_refresh(): rebuild the ttmod_menu literal on this state.
static int __cdecl fn_refresh(lua_State* L) {
    if (!g_loadstring || !g_pcallk || !g_gettop) return 0;
    unsigned seq = (unsigned)InterlockedIncrement(&g_seq);
    auto snap = snapshot();
    std::string chunk = ttmod::build_menu_literal(snap, seq);
    run_chunk(L, "refresh", chunk.c_str());
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
    std::string next = ttmod::apply_enabled_change(read_file(path), id, en);
    if (!write_file(path, next)) {
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
    ttmod::ModManifest m;
    int n = mods_menu_count();
    bool found = false;
    for (int i = 0; i < n; ++i) {
        if (mods_menu_manifest(i, &m) && m.id == id) {
            found = true;
            break;
        }
    }
    if (!found || m.config.empty()) return 0;
    std::string path = g_gamedir + "\\config\\" + std::string(id) + ".json";
    ttmod::SetValueResult r = ttmod::apply_config_value(m.config, read_file(path), key, val);
    if (!r.ok) {
        char msg[256];
        snprintf(msg, sizeof msg, "menumods: value rejected %s.%s", id, key);
        emit(msg);
        return 0;
    }
    if (!write_file(path, r.file_text)) {
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

static void reg_fn(lua_State* L, LuaPushCClosureFn pushcclosure, LuaSetglobalFn setglobal,
                   lua_CFunction fn, const char* name) {
    if (!pushcclosure || !setglobal) return;
    pushcclosure(L, fn, 0);
    setglobal(L, name);
}

} // namespace

void menumods_init(const char* game_dir, const char* log_path) {
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
        } else if (GetFileAttributesA((g_gamedir + "\\config\\menu-disabled").c_str()) !=
                   INVALID_FILE_ATTRIBUTES) {
            enabled = false;
            emit("menumods: menu button disabled via config/menu-disabled");
        }
    }
    return enabled;
}

void menumods_register(lua_State* L, LuaLoadstringFn loadstring, LuaPcallkFn pcallk,
                       LuaGettopFn gettop, LuaTolstringFn tolstring,
                       LuaPushCClosureFn pushcclosure, LuaSetglobalFn setglobal) {
    if (!L || !loadstring || !pcallk || !gettop || !tolstring || !pushcclosure || !setglobal)
        return;
    g_loadstring = loadstring;
    g_pcallk = pcallk;
    g_gettop = gettop;
    g_tolstring = tolstring;
    g_setglobal = setglobal;
    reg_fn(L, pushcclosure, setglobal, fn_refresh, "ttmod_menu_refresh");
    reg_fn(L, pushcclosure, setglobal, fn_set_enabled, "ttmod_menu_set_enabled");
    reg_fn(L, pushcclosure, setglobal, fn_set_value, "ttmod_menu_set_value");
    reg_fn(L, pushcclosure, setglobal, fn_log, "ttmod_menu_log");
    run_chunk(L, "ui-defs", kMenuModsUi);
    emit("menumods: ui registered on state");
}

} // namespace ttmod_win
#endif
