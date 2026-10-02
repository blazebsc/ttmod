// title-mcsm example mod (M4): first real, visible modification.
// Polls FindWindowA for the known game window title and prefixes it with
// "[TTMod]", verified by read-back. Cosmetic only; never touches game state.
//
// NOTE: a WH_CBT Hook version was tried and REVERTED: threadId=0 makes the
// hook desktop-wide in Wine (fires in every GUI process, heavy/deadly under
// Wine: correlated with hangs + early exits). Polling our own process is safe.
// Migrates to framework events in M7.
#include "ttmod/plugin_api.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

const ttmod_host* g_host = nullptr;
bool g_done = false;

static void say(const char* msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

static void try_title(HWND hwnd) {
    if (g_done || !IsWindow(hwnd)) return;
    char cur[192] = {};
    if (GetWindowTextA(hwnd, cur, sizeof cur) <= 0) return;
    if (strstr(cur, "[TTMod]") != nullptr) {
        g_done = true;
        return;
    }
    char want[224] = {};
    snprintf(want, sizeof want, "[TTMod] %s", cur);
    SetWindowTextA(hwnd, want);
    char back[224] = {};
    GetWindowTextA(hwnd, back, sizeof back);
    char m[640];
    snprintf(m, sizeof m, "title-mcsm: title changed: '%s' -> '%s' (readback '%s')", cur, want, back);
    say(m);
    g_done = true;
}

static DWORD WINAPI watch(LPVOID) {
    say("title-mcsm: watching for game window (poll FindWindowA)");
    for (int i = 0; i < 600 && !g_done; ++i) { // ~60s at 100ms
        HWND w = FindWindowA(nullptr, "Telltale Games");
        if (w) {
            char m[128];
            snprintf(m, sizeof m, "title-mcsm: found game window %p", (void*)w);
            say(m);
            try_title(w);
        }
        Sleep(100);
    }
    if (!g_done) say("title-mcsm: game window not found, giving up");
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version != TTMOD_PLUGIN_API_VERSION) return -2;
    g_host = host;
    say("title-mcsm: init, spawning window watch");
    HANDLE t = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr);
    if (!t) {
        say("title-mcsm: failed to spawn watch thread");
        return -3;
    }
    CloseHandle(t);
    return 0;
}
