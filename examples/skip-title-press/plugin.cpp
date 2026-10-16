// skip-title-press: auto-dismiss the press-anywhere title screen.
//
// The title gate (bootTitle.lua: titleShow chore + pressStart.imap) has no
// Lua-callable continue that a script mod can reach in time (drained chunks
// run before the boot env exists; see game.lua attempts). So this plugin
// does what a finger does: posts Enter to the game window once the title
// is up. Stand-down is log-driven: the moment the menu state registers
// ("menumods: ui registered on state") the user is already through and we
// quit quietly, so a shot can never land in the menu and activate a row.
//
// Init never blocks (spawns a thread, returns 0). Worst case the shots
// miss and the game behaves stock.
//
// Stand-down signal is "Menu_Add wrapper offered": Menu.lua loads on the
// menu state only once the switch to Menu happens, i.e. the title is
// gone. (Do NOT use "ui registered on state" - the menu state is
// observed during boot, long before the switch.)
#include "ttmod/plugin_api.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

const ttmod_host* g_host = nullptr;

static void say(const char* msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

// True if the tail of ttmod.log shows the menu booting (post-switch).
static bool menu_up(const char* logpath) {
    HANDLE h =
        CreateFileA(logpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD hi = 0;
    DWORD lo = GetFileSize(h, &hi);
    unsigned long long size = ((unsigned long long)hi << 32) | lo;
    unsigned long long want = size > 65536 ? 65536 : size;
    LONG off = (LONG)(size - want);
    SetFilePointer(h, off, nullptr, FILE_BEGIN);
    char buf[65536];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)want, &got, nullptr);
    CloseHandle(h);
    if (!ok || got < 32) return false;
    buf[got] = '\0';
    return strstr(buf, "lua: Menu_Add wrapper offered") != nullptr;
}

static void press_once() {
    HWND w = FindWindowA(nullptr, "Telltale Games");
    if (!w) {
        say("skip-title-press: game window not found for shot");
        return;
    }
    // Volley: the title imap maps a few inputs to continue; the menu
    // ignores nothing, so shots only fire while no menu signal is seen.
    const UINT keys[] = {VK_RETURN, VK_SPACE};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        PostMessageA(w, WM_KEYDOWN, keys[i], 0);
        PostMessageA(w, WM_KEYUP, keys[i], 0);
    }
    say("skip-title-press: posted Enter+Space volley to game window");
}

static DWORD WINAPI watch(LPVOID) {
    char exepath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exepath, sizeof exepath);
    char* slash = strrchr(exepath, '\\');
    if (!slash) {
        say("skip-title-press: cannot locate exe dir, giving up");
        return 0;
    }
    *slash = '\0';
    char logpath[MAX_PATH] = {};
    snprintf(logpath, sizeof logpath, "%s\\logs\\ttmod.log", exepath);
    say("skip-title-press: watching for title (volleys at ~15s/25s/35s unless menu boots)");
    int shots = 0;
    for (int i = 0; i < 90; ++i) { // ~90s at 1s
        Sleep(1000);
        if (menu_up(logpath)) {
            say("skip-title-press: menu booting, standing down");
            return 0;
        }
        int elapsed = i + 1;
        if (shots < 3 && elapsed >= 15 + shots * 10) {
            press_once();
            ++shots;
        }
    }
    say("skip-title-press: timed out, giving up (game behaves stock)");
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 1) return -2;
    g_host = host;
    HANDLE t = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr);
    if (!t) {
        say("skip-title-press: failed to spawn watch thread");
        return -3;
    }
    CloseHandle(t);
    return 0;
}
