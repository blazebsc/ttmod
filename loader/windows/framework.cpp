// ttmod framework DLL (M2). Windows-only; NOT built on Linux.
// DllMain does the minimum: resolve exe path + log dir, run portable
// ttmod::init_from_exe (detect -> profile -> log), log module base/size,
// then run the hook stage (validates + installs, or logs why not).
// Any failure is logged and the game continues unmodified.
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include "ttmod/profile.hpp"
#include "ttmod/log.hpp"
#include "ttmod/discovery.hpp"
#include "ttmod/cache.hpp"
#include "ttmod/modstate.hpp"
#include "events.hpp"
#include "hooks.hpp"
#include "lua_bridge.hpp"
#include "menu_bridge.hpp"
#include "mods.hpp"
#include "stage.hpp"
#include "modscan.hpp"
#include "plugins.hpp"

static std::string dir_of(const char* path) {
    std::string s = path ? path : "";
    size_t i = s.find_last_of("\\/");
    return i == std::string::npos ? std::string(".") : s.substr(0, i);
}

static DWORD g_t0 = 0;
static char g_exitlog[MAX_PATH] = {};

// Exit-time evidence (raw Win32 only: no msvcrt, no hooks, no alloc).
// Records session length + target-region state to distinguish
// "unpack never finished" from "watcher never saw it".
static void exit_dump() {
    if (!g_exitlog[0]) return;
    HMODULE exe = GetModuleHandleA(nullptr);
    char buf[256];
    int n = 0;
    DWORD dt = GetTickCount() - g_t0;
    if (exe) {
        // Image pages are committed in our own process; direct read is safe.
        const BYTE* t = (const BYTE*)exe + 0x1139F0;
        n = snprintf(buf, sizeof buf,
                     "exit: dt=%lums target=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                     (unsigned long)dt, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
    } else {
        n = snprintf(buf, sizeof buf, "exit: no exe module\r\n");
    }
    HANDLE f = CreateFileA(g_exitlog, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(f, buf, (DWORD)n, &w, nullptr);
        char sbuf[512];
        int m = ttmod_win::events_state_summary(sbuf, sizeof sbuf);
        if (m > 0) WriteFile(f, sbuf, (DWORD)m, &w, nullptr);
        CloseHandle(f);
    }
}

static DWORD WINAPI InitThread(LPVOID self) {
    g_t0 = GetTickCount();
    ttmod_win::stage_mark_start();
    // M6 §6: loaded-module survey (env-gated). Answers "is Lua in a DLL?".
    // Module/base diagnostic: never assume 0x400000 — read the headers.
    HMODULE exe = GetModuleHandleA(nullptr);
    char base[128] = {};
    if (exe) {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)exe;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            PIMAGE_NT_HEADERS nt =
                (PIMAGE_NT_HEADERS)((BYTE*)exe + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                snprintf(base, sizeof base, "exe_base=%p image_size=0x%08lX",
                         (void*)exe, (unsigned long)nt->OptionalHeader.SizeOfImage);
        }
        if (!base[0])
            snprintf(base, sizeof base, "exe_base=%p (headers unreadable)", (void*)exe);
    }
    char dll[MAX_PATH] = {};
    GetModuleFileNameA((HMODULE)self, dll, MAX_PATH);
    char exepath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exepath, MAX_PATH);
    std::string gamedir = dir_of(exepath[0] ? exepath : ".");
    // M11 layout: user dirs are siblings; framework creates them on first run.
    CreateDirectoryA((gamedir + "\\mods").c_str(), nullptr);
    CreateDirectoryA((gamedir + "\\config").c_str(), nullptr);
    CreateDirectoryA((gamedir + "\\logs").c_str(), nullptr);
    CreateDirectoryA((gamedir + "\\ttmod").c_str(), nullptr);
    CreateDirectoryA((gamedir + "\\ttmod\\cache").c_str(), nullptr);
    std::string logpath = gamedir + "\\logs\\ttmod.log";
    std::string exlog = gamedir + "\\logs\\ttmod.exit.log";
    strncpy(g_exitlog, exlog.c_str(), sizeof g_exitlog - 1);
    // M6 §6: loaded-module survey (env-gated). Answers "is Lua in a DLL?".
    char modlist[32] = {};
    if (GetEnvironmentVariableA("TTMOD_MODLIST", modlist, sizeof modlist) > 0) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 me;
            me.dwSize = sizeof me;
            ttmod::Logger lg;
            if (lg.open(logpath) && Module32First(snap, &me)) {
                do {
                    char m[300];
                    snprintf(m, sizeof m, "modlist: %p size=0x%08X %s", me.modBaseAddr,
                             (unsigned)me.modBaseSize, me.szModule);
                    lg.info(m);
                } while (Module32Next(snap, &me));
            }
            CloseHandle(snap);
        }
    }
    std::string prof = ttmod::init_from_exe(exepath[0] ? exepath : "unknown", logpath);
    if (base[0]) {
        ttmod::Logger log;
        if (log.open(logpath)) log.info(base);
    }
    ttmod_win::events_init(logpath.c_str());
    ttmod_win::hooks_init(prof.c_str(), logpath.c_str());
    ttmod_win::lua_bridge_init(prof.c_str(), logpath.c_str());
    ttmod_win::stage(logpath.c_str(), "hooks+bridge init returned");
    const char* game = "unknown";
    int season = 0;
    if (prof == "mcsm1_pc_x86") { game = "minecraft-story-mode"; season = 1; }

    // M11 canonical discovery: game-root/mods (.ttmod + unpacked dirs).
    ttmod::ModState state;
    {
        std::string sp = gamedir + "\\config\\mods.json";
        FILE* f = fopen(sp.c_str(), "rb");
        if (f) {
            std::string t;
            char b[1024];
            size_t r;
            while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
            fclose(f);
            ttmod::StateFile sf = ttmod::parse_state(t);
            if (sf.ok) state = sf.state;
        }
    }
    ttmod::Discovery disc = ttmod::discover_mods(gamedir + "\\mods", state, game, season);
    // M24 safe mode: env var or config/safe-mode file disables ALL
    // third-party mods (framework hooks stay; game always continues).
    char safeenv[8] = {};
    bool safe = GetEnvironmentVariableA("TTMOD_SAFE_MODE", safeenv, sizeof safeenv) > 0;
    if (!safe) {
        DWORD sa = GetFileAttributesA((gamedir + "\\config\\safe-mode").c_str());
        safe = sa != INVALID_FILE_ATTRIBUTES;
    }
    if (safe) {
        ttmod::Logger lg;
        if (lg.open(logpath)) lg.info("[TTMod] SAFE MODE: all third-party mods disabled");
        disc.mods.clear();
        disc.skipped.push_back("all: safe mode");
    }
    {
        ttmod::Logger lg;
        if (lg.open(logpath)) {
            char h[160];
            int pkgs = 0;
            for (auto& m : disc.mods)
                if (m.packaged) pkgs++;
            snprintf(h, sizeof h, "[TTMod] Scanning mods/ (%d entries)", disc.entries_seen);
            lg.info(h);
            for (auto& s : disc.skipped) lg.info(std::string("[TTMod] Skipped: ") + s);
            int disabled = 0;
            for (auto& s : disc.skipped)
                if (s.find(": disabled") != std::string::npos) disabled++;
            snprintf(h, sizeof h, "[TTMod] Valid mods: %u (%d packaged) Disabled: %d Skipped: %u",
                     (unsigned)disc.mods.size(), pkgs, disabled, (unsigned)disc.skipped.size());
            lg.info(h);
        }
    }
    // Packaged mods extract to ttmod/cache/<id> (stale entries cleaned).
    std::vector<std::pair<std::string, std::string>> pkgs;
    for (auto& m : disc.mods)
        if (m.packaged) pkgs.emplace_back(m.id, m.source);
    ttmod::CacheSync cache =
        ttmod::sync_package_cache(gamedir + "\\ttmod\\cache", pkgs);
    {
        ttmod::Logger lg;
        if (lg.open(logpath))
            for (auto& l : cache.log) lg.info(std::string("[TTMod] cache: ") + l);
    }
    // Uniform entries for both loaders (cache dirs for packages).
    std::vector<ttmod_win::ScannedMod> all;
    for (auto& m : disc.mods) {
        std::string dir = m.source;
        if (m.packaged) {
            auto it = cache.effective.find(m.id);
            if (it == cache.effective.end()) continue; // sync failed, logged
            dir = it->second;
        }
        std::string prel = m.manifest.plugin.empty() ? "plugin.dll" : m.manifest.plugin;
        std::string winrel = prel;
        for (char& c : winrel)
            if (c == '/') c = '\\';
        DWORD a = GetFileAttributesA((dir + "\\" + winrel).c_str());
        bool has_dll = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        if (!m.manifest.plugin.empty() && !has_dll) {
            ttmod::Logger lg;
            if (lg.open(logpath))
                lg.info("[TTMod] " + m.id + ": declared plugin missing: " + m.manifest.plugin);
        }
        all.push_back({dir, has_dll, m.packaged, has_dll ? prel : "", m.manifest});
    }
    std::sort(all.begin(), all.end(), [](const ttmod_win::ScannedMod& a, const ttmod_win::ScannedMod& b) {
        return a.manifest.id < b.manifest.id;
    });
    ttmod_win::plugins_init(all, prof.c_str(), game, season, logpath.c_str());
    ttmod_win::mods_init(all, gamedir.c_str(), logpath.c_str());
    ttmod_win::mods_store_menu(all, disc.disabled);
    ttmod_win::menumods_init(gamedir.c_str(), logpath.c_str());
    ttmod_win::stage(logpath.c_str(), "init thread done (mods+plugins ready)");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        HANDLE t = CreateThread(nullptr, 0, InitThread, self, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        ttmod_win::lua_bridge_shutdown();
        exit_dump();
    }
    return TRUE; // never block the game
}
#endif
