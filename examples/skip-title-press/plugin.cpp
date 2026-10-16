// skip-title-press: auto-dismiss the press-anywhere title screen.
//
// The title gate (bootTitle.lua: titleShow chore + pressStart.imap) has no
// Lua-callable continue that a script mod can reach in time (drained chunks
// run before the boot env exists). So this plugin does what a finger does:
// posts input to the game window once the title is up.
//
// No log line reliably separates "title up" from "menu up" (menu scripts
// pre-warm during boot, so every candidate signal fires early), so there
// is no stand-down: exactly two volleys (12s, 25s), then silence. Shots
// use a corner click (inert in menus) plus Enter/Space; worst case the
// volleys miss and the game behaves stock.
//
// Init never blocks (spawns a thread, returns 0).
#include "ttmod/plugin_api.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

const ttmod_host* g_host = nullptr;

static void say(const char* msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

static void press_once() {
    HWND w = FindWindowA(nullptr, "Telltale Games");
    if (!w) {
        say("skip-title-press: game window not found for shot");
        return;
    }
    // Volley: corner click (inert in menus) plus Enter/Space.
    LPARAM at_corner = MAKELPARAM(10, 10);
    PostMessageA(w, WM_LBUTTONDOWN, 0, at_corner);
    PostMessageA(w, WM_LBUTTONUP, 0, at_corner);
    const UINT keys[] = {VK_RETURN, VK_SPACE};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        PostMessageA(w, WM_KEYDOWN, keys[i], 0);
        PostMessageA(w, WM_KEYUP, keys[i], 0);
    }
    say("skip-title-press: posted click+Enter+Space volley to game window");
}

static DWORD WINAPI watch(LPVOID) {
    say("skip-title-press: watching for title (volleys at ~12s/25s)");
    int shots = 0;
    for (int i = 0; i < 90; ++i) { // ~90s at 1s
        Sleep(1000);
        int elapsed = i + 1;
        if (shots < 2 && elapsed >= 12 + shots * 13) {
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
