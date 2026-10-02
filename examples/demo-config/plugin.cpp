// demo-config mod: owns config/demo-config.json with one value per type
// (string/int/bool). Logs the parsed values on init — the modmenu-core
// listing picks this mod up automatically, proving menu + config meet.
// Copy config-example.json to <game>/config/demo-config.json and edit it;
// relaunch and compare the log lines. Missing file = defaults (logged).
// Hand parser like titleprefix (no JSON lib in plugins by design).
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

// First integer after "key". Returns def when absent.
static int get_int(const std::string& t, const char* key, int def) {
    size_t k = t.find(key);
    if (k == std::string::npos) return def;
    size_t p = t.find_first_of("-0123456789", k + strlen(key));
    if (p == std::string::npos) return def;
    int v = 0, neg = 0;
    if (t[p] == '-') {
        neg = 1;
        ++p;
    }
    int any = 0;
    for (; p < t.size() && t[p] >= '0' && t[p] <= '9'; ++p) {
        v = v * 10 + (t[p] - '0');
        any = 1;
    }
    return any ? (neg ? -v : v) : def;
}

// First true/false literal after "key". Returns def when absent.
static int get_bool(const std::string& t, const char* key, int def) {
    size_t k = t.find(key);
    if (k == std::string::npos) return def;
    size_t p = k + strlen(key);
    size_t tt = t.find("true", p), ff = t.find("false", p);
    size_t e = t.find_first_of("},", p);
    if (tt != std::string::npos && (e == std::string::npos || tt < e) &&
        (ff == std::string::npos || tt < ff))
        return 1;
    if (ff != std::string::npos && (e == std::string::npos || ff < e)) return 0;
    return def;
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    g_host = host;
    std::string greeting = "Hello from demo-config!";
    int level = 3, fancy = 1, from_file = 0;
    std::string root = game_root();
    if (!root.empty()) {
        std::string t = read_all(root + "\\config\\demo-config.json");
        if (!t.empty()) {
            from_file = 1;
            std::string g = get_str(t, "\"greeting\"");
            if (!g.empty()) greeting = g;
            level = get_int(t, "\"level\"", level);
            fancy = get_bool(t, "\"fancy\"", fancy);
        }
    }
    char m[320];
    snprintf(m, sizeof m,
             "demo-config: greeting='%s' level=%d fancy=%s (%s)", greeting.c_str(), level,
             fancy ? "true" : "false", from_file ? "config/demo-config.json" : "defaults, no config file");
    say(m);
    return 0;
}
