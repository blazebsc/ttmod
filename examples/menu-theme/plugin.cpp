// menu-theme mod: reads config/menu-theme.json {"accent": "#RRGGBB"} and
// tints the native Mods menu by setting _G.TTMOD_ACCENT (see
// loader/windows/menumods_ui.lua: absent = stock, present = first engine-
// accepted color property wins for the session). Missing file / missing key
// = manifest default #E0A040. Garbage = log + stay unloaded. Needs host api
// >= 5 (queue_ui_chunk); older hosts = log + stay unloaded.
// Hand parser like titleprefix/demo-config (no JSON lib in plugins).
#include "ttmod/plugin_api.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

const ttmod_host* g_host = nullptr;

static void say(const char* msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

static std::string game_root() {
    char path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&game_root, &self);
    if (!self || !GetModuleFileNameA(self, path, sizeof path)) return "";
    std::string s = path;
    for (char& c : s)
        if (c == '/') c = '\\';
    const char* marks[] = {"\\mods\\", "\\ttmod\\cache\\"};
    for (auto* m : marks) {
        size_t i = s.find(m);
        if (i != std::string::npos) return s.substr(0, i);
    }
    size_t i = s.find_last_of('\\');
    return i == std::string::npos ? "" : s.substr(0, i);
}

static std::string read_all(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return "";
    std::string t;
    char b[256];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
    fclose(f);
    return t;
}

// First quoted string after "key". "" = absent.
static std::string get_str(const std::string& t, const char* key) {
    size_t k = t.find(key);
    if (k == std::string::npos) return "";
    size_t q1 = t.find('"', k + strlen(key) + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = t.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return t.substr(q1 + 1, q2 - q1 - 1);
}

static bool valid_accent(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 5 || !host->queue_ui_chunk) {
        host->log("menu-theme: needs host api >= 5 (ui chunk), staying unloaded");
        return -2;
    }
    g_host = host;
    std::string accent = "#E0A040";
    const char* src = "default";
    std::string root = game_root();
    if (!root.empty()) {
        std::string t = read_all(root + "\\config\\menu-theme.json");
        if (!t.empty()) {
            std::string a = get_str(t, "\"accent\"");
            if (!a.empty()) {
                accent = a;
                src = "config/menu-theme.json";
            }
        }
    }
    if (!valid_accent(accent)) {
        char m[192];
        snprintf(m, sizeof m, "menu-theme: bad accent '%s' (want #RRGGBB), staying unloaded",
                 accent.c_str());
        say(m);
        return -3;
    }
    // Validated #RRGGBB: no quoting risk.
    char chunk[64];
    snprintf(chunk, sizeof chunk, "_G.TTMOD_ACCENT = \"%s\"", accent.c_str());
    int q = host->queue_ui_chunk(chunk);
    char m[192];
    snprintf(m, sizeof m, "menu-theme: accent '%s' from %s (%s)", accent.c_str(), src,
             q == 0 ? "ui chunk queued" : "ui chunk dropped");
    say(m);
    return 0;
}
