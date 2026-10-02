// See lua_bridge.hpp for the design contract.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "ttmod/log.hpp"
#include "ttmod/lua_bridge.hpp"
#include "ttmod/uiqueue.hpp"
#include "lua_abi.hpp"
#include "lua_bridge.hpp"
#include "menu_bridge.hpp"
#include "stage.hpp"

namespace ttmod_win {
namespace {

// Opaque game Lua state; Lua 5.2 x86 cdecl ABI (see lua_abi.hpp).
using NewstateFn = LuaNewstateFn;
using LoadstringFn = LuaLoadstringFn;
using PcallkFn = LuaPcallkFn;
using GettopFn = LuaGettopFn;
using TolstringFn = LuaTolstringFn;
using PushCClosureFn = LuaPushCClosureFn;
using SetglobalFn = LuaSetglobalFn;

static std::string g_logpath;
static NewstateFn g_origNewstate = nullptr;
static LoadstringFn g_fnLoadstring = nullptr;
static PcallkFn g_fnPcallk = nullptr;
static GettopFn g_fnGettop = nullptr;
static TolstringFn g_fnTolstring = nullptr;
static PushCClosureFn g_fnPushCClosure = nullptr;
static SetglobalFn g_fnSetglobal = nullptr;
static lua_State* volatile g_state = nullptr;
static volatile LONG g_states_seen = 0;
static volatile LONG g_test_done = 0;
static volatile LONG g_dead = 0;

static void emit(const std::string& msg) {
    ttmod::Logger log;
    if (log.open(g_logpath)) log.info(msg);
}

// ScriptManager::LoadResource detour (WIN32 cdecl, verified against
// unpacked prologue + stack-arg usage). Calls orig first, then offers the
// Menu_Main wrapper exactly once when that script finishes loading.
using LoadResourceFn = int(__cdecl*)(lua_State*, char*);
static LoadResourceFn g_origLoadResource = nullptr;
static volatile LONG g_loadlog = 0;

static bool tail_matches(const char* path, const char* tail) {
    if (!path || !tail) return false;
    size_t n = strlen(path), m = strlen(tail);
    return n >= m && strcmp(path + n - m, tail) == 0;
}

// Append-report sink for the Menu_Main wrapper (same-thread, never errors).
static int __cdecl append_log(lua_State* L) {
    const char* s = g_fnTolstring ? g_fnTolstring(L, 1, nullptr) : nullptr;
    char m[160];
    snprintf(m, sizeof m, "lua-probe: %p %s", (void*)L, s ? s : "?");
    emit(m);
    return 0;
}

static int __cdecl hook_loadresource(lua_State* L, char* filename) {
    int rc = g_origLoadResource(L, filename);
    if (!L || g_dead) return rc;
    if (InterlockedIncrement(&g_loadlog) <= 5) {
        char m[160];
        snprintf(m, sizeof m, "lua: loadresource #%ld %s", (long)g_loadlog, filename ? filename : "?");
        emit(m);
    }
    // Plugin chunk queue (v5): drained on the game's script thread right
    // after the script load, on that state. Before the Menu.lua branch so
    // observe-only mode (TTMOD_LUA_LRCHUNK=0) still drains plugin chunks.
    for (const std::string& c : ttmod::uiqueue_take())
        bridge_run_chunk(L, g_fnLoadstring, g_fnPcallk, g_fnGettop, g_fnSetglobal, g_fnTolstring,
                         "plugin", c.c_str());
    // Menu_Add wrapper: Menu.lua defines Menu_Add. Suffix "Menu.lua" does
    // NOT match "Menu_Main.lua" (ends in "Main.lua"), so only Menu.lua
    // itself triggers; the chunk one-shot guard covers reloads anyway.
    if (filename && tail_matches(filename, "Menu.lua") && g_fnLoadstring && g_fnPcallk &&
        g_fnGettop && g_fnPushCClosure && g_fnSetglobal && g_fnTolstring) {
        // TTMOD_LUA_LRCHUNK=0: observe-only (detour stays, no chunk runs).
        char nochunk[8] = {};
        if (GetEnvironmentVariableA("TTMOD_LUA_LRCHUNK", nochunk, sizeof nochunk) > 0 &&
            strcmp(nochunk, "0") == 0) {
            emit("lua: wrapper chunk skipped via TTMOD_LUA_LRCHUNK=0");
            return rc;
        }
        if (!menumods_button_enabled()) {
            emit("lua: Menu_Add wrapper skipped (menu disabled)");
            return rc;
        }
        g_fnPushCClosure(L, append_log, 0);
        g_fnSetglobal(L, "Menu_Main_AppendLog");
        bridge_run_chunk(L, g_fnLoadstring, g_fnPcallk, g_fnGettop, g_fnSetglobal, g_fnTolstring,
                         "Menu_Add wrapper", ttmod_win::kMenuAddWrapChunk);
        char m[96];
        snprintf(m, sizeof m, "lua: Menu_Add wrapper offered");
        emit(m);
    }
    return rc;
}

static lua_State* __cdecl hook_lua_newstate(lua_Alloc alloc, void* ud) {
    lua_State* L = g_origNewstate(alloc, ud);
    if (!L || g_dead) return L;
    InterlockedExchangePointer((PVOID volatile*)&g_state, L);
    LONG n = InterlockedIncrement(&g_states_seen);
    if (n == 1) {
        emit("lua: lua_newstate observed, live state captured");
        stage(g_logpath.c_str(), "lua_newstate observed");
    }
    // One-shot proof on the fresh state, on this same game thread.
    if (InterlockedCompareExchange(&g_test_done, 1, 0) == 0 && g_fnLoadstring && g_fnPcallk &&
        g_fnGettop) {
        int t0 = g_fnGettop(L);
        int lr = g_fnLoadstring(L, ttmod::kLuaBridgeTestChunk);
        int pr = -1;
        if (lr == 0) pr = g_fnPcallk(L, 0, 0, 0, 0, nullptr);
        int t1 = g_fnGettop(L);
        if (t1 != t0 && g_fnSetglobal) {
            g_fnSetglobal(L, "ttmod_last_error"); // sink stray error, keep balance
            t1 = g_fnGettop(L);
        }
        if (lr == 0 && pr == 0 && t1 == t0) {
            emit("lua: bridge ready (executed ttmod_bridge_ok=true, stack balanced)");
            stage(g_logpath.c_str(), "bridge proof executed");
        } else {
            char m[256] = {};
            const char* err = "";
            if (g_fnTolstring && t1 > t0) err = g_fnTolstring(L, -1, nullptr);
            snprintf(m, sizeof m, "lua: bridge test FAILED load=%d pcall=%d top=%d->%d err=%s", lr,
                     pr, t0, t1, err ? err : "?");
            emit(m);
        }
    }
    // Mods-menu UI: idempotent defs on EVERY captured state so whichever
    // state hosts the menu gets the entry points + screens. Same-thread,
    // balanced-stack rules as the proof above.
    menumods_register(L, g_fnLoadstring, g_fnPcallk, g_fnGettop, g_fnTolstring, g_fnPushCClosure,
                      g_fnSetglobal);
    return L;
}

struct LateHook {
    HMODULE exe;
};

static DWORD WINAPI late_hook_thread(LPVOID p) {
    LateHook* h = (LateHook*)p;
    Sleep(2500); // proven late-install window (init-time game-code detours hang Wine)
    if (g_dead) {
        delete h;
        return 0;
    }
    char off[32] = {};
    if (GetEnvironmentVariableA("TTMOD_LUA_BRIDGE", off, sizeof off) > 0 && strcmp(off, "0") == 0) {
        emit("lua: disabled via TTMOD_LUA_BRIDGE=0");
        delete h;
        return 0;
    }
    BYTE* base = (BYTE*)h->exe;
    const ttmod::LuaBridgeAddrs addrs;
    // Anchor-validate the primary target from live memory before patching.
    // The exe unpacks progressively (packed file bytes differ from live
    // code): retry at 2.5s / 5s / 8s to fit the typical ~15s session
    // window; first prologue match wins.
    static const DWORD kRetries[] = {2500, 2500, 3000};
    bool ok = false;
    for (int i = 0; i < 3 && !g_dead; ++i) {
        if (i) Sleep(kRetries[i]);
        if (memcmp(base + ttmod::kLuaNewstateAnchor.rva, ttmod::kLuaNewstateAnchor.bytes, 4) ==
            0) {
            ok = true;
            break;
        }
        char m[96];
        snprintf(m, sizeof m, "lua: anchor try%d bytes=%02X %02X %02X %02X", i,
                 base[0x611C80], base[0x611C81], base[0x611C82], base[0x611C83]);
        emit(m);
    }
    if (!ok) {
        emit("lua: newstate anchor mismatch, installing nothing");
        delete h;
        return 0;
    }
    g_fnLoadstring = (LoadstringFn)(base + addrs.loadstring);
    g_fnPcallk = (PcallkFn)(base + addrs.pcallk);
    g_fnGettop = (GettopFn)(base + addrs.gettop);
    g_fnTolstring = (TolstringFn)(base + addrs.tolstring);
    g_fnPushCClosure = (PushCClosureFn)(base + addrs.pushcclosure);
    g_fnSetglobal = (SetglobalFn)(base + addrs.setglobal);
    void* target = (void*)(base + addrs.newstate);
    if (MH_CreateHook(target, (LPVOID)hook_lua_newstate, (LPVOID*)&g_origNewstate) != MH_OK) {
        emit("lua: MH_CreateHook(newstate) failed, installing nothing");
        delete h;
        return 0;
    }
    if (MH_EnableHook(target) != MH_OK) {
        emit("lua: MH_EnableHook(newstate) failed, installing nothing");
        delete h;
        return 0;
    }
    emit("lua: hook installed (late, anchor-verified)");
    // Optional split for diagnosis (TTMOD_LUA_LRHOOK=0): skip ONLY the
    // ScriptManager::LoadResource hook (no per-script chunks, no Menu_Add
    // wrapper). The lua_newstate bridge above is unaffected.
    char lr[8] = {};
    if (GetEnvironmentVariableA("TTMOD_LUA_LRHOOK", lr, sizeof lr) > 0 && strcmp(lr, "0") == 0) {
        emit("lua: loadresource hook skipped via TTMOD_LUA_LRHOOK=0");
        delete h;
        return 0;
    }
    // Script-load hook (same late window + live-anchor gate as newstate):
    // after Menu_Main.lua finishes loading, install the Menu_Main wrapper
    // exactly once. Loader-thread, same-state execution.
    if (memcmp(base + ttmod::kLoadResourceLiveAnchor.rva, ttmod::kLoadResourceLiveAnchor.bytes,
               4) == 0) {
        void* lr = (void*)(base + 0x1139F0);
        if (MH_CreateHook(lr, (LPVOID)hook_loadresource, (LPVOID*)&g_origLoadResource) == MH_OK &&
            MH_EnableHook(lr) == MH_OK) {
            emit("lua: loadresource hook installed (late, anchor-verified)");
        } else {
            emit("lua: MH_CreateHook(loadresource) failed, continuing without it");
        }
    } else {
        char m[96];
        snprintf(m, sizeof m, "lua: loadresource anchor mismatch %02X %02X %02X %02X, skipping",
                 base[0x1139F0], base[0x1139F1], base[0x1139F2], base[0x1139F3]);
        emit(m);
    }
    delete h;
    return 0;
}

} // namespace

void lua_bridge_init(const char* profile_id, const char* log_path) {
    g_logpath = log_path ? log_path : "";
    if (!profile_id || strcmp(profile_id, "mcsm1_pc_x86") != 0) return;
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) {
        emit("lua: no exe module, skipping");
        return;
    }
    HANDLE t = CreateThread(nullptr, 0, late_hook_thread, new LateHook{exe}, 0, nullptr);
    if (t)
        CloseHandle(t);
    else
        emit("lua: late thread failed, skipping");
}

void lua_bridge_shutdown() {
    InterlockedExchange(&g_dead, 1);
    InterlockedExchangePointer((PVOID volatile*)&g_state, nullptr);
}

} // namespace ttmod_win
#endif
