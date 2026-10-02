// title-prefix demo config mod: reads config/titleprefix.json {"prefix": "..."}
// and titles the game window with it (verified by read-back, like M4).
// Edit the JSON, relaunch, observe the new title. No host features needed.
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

// Tiny {"prefix": "..."} reader (first string value after the key).
static std::string read_prefix(const std::string& cfg) {
    FILE* f = fopen(cfg.c_str(), "rb");
    if (!f) return "";
    std::string t;
    char b[256];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
    fclose(f);
    size_t k = t.find("\"prefix\"");
    if (k == std::string::npos) return "";
    size_t q1 = t.find('"', k + 8);
    if (q1 == std::string::npos) return "";
    size_t q2 = t.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return t.substr(q1 + 1, q2 - q1 - 1);
}

static DWORD WINAPI watch(LPVOID) {
    std::string root = game_root();
    std::string prefix = "[TTMod] ";
    if (!root.empty()) {
        std::string p = read_prefix(root + "\\config\\titleprefix.json");
        if (!p.empty()) prefix = p;
    }
    char logged[256];
    snprintf(logged, sizeof logged, "title-prefix: using prefix '%s'", prefix.c_str());
    say(logged);
    for (int i = 0; i < 600; ++i) {
        HWND w = FindWindowA(nullptr, "Telltale Games");
        if (w) {
            char cur[192] = {};
            GetWindowTextA(w, cur, sizeof cur);
            if (cur[0] && strstr(cur, prefix.c_str()) != cur) {
                char want[224] = {};
                snprintf(want, sizeof want, "%s%s", prefix.c_str(), cur);
                SetWindowTextA(w, want);
                char back[224] = {};
                GetWindowTextA(w, back, sizeof back);
                char m[640];
                snprintf(m, sizeof m, "title-prefix: title -> '%s' (readback '%s')", want, back);
                say(m);
                return 0;
            }
            if (cur[0]) return 0; // already prefixed (or changed): done
        }
        Sleep(100);
    }
    say("title-prefix: game window not found, giving up");
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    g_host = host;
    say("title-prefix: init, watching for game window");
    HANDLE t = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr);
    if (!t) {
        say("title-prefix: thread failed");
        return -3;
    }
    CloseHandle(t);
    return 0;
}
