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
#include "win32_path.hpp"

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
        // Structured snapshot: the primary machine artifact. Human line above
        // stays byte-identical for existing grep.
        char jbuf[768];
        int j = ttmod_win::events_snapshot_json(jbuf, sizeof jbuf);
        if (j > 0) WriteFile(f, jbuf, (DWORD)j, &w, nullptr);
        CloseHandle(f);
    }
}

// InitThread outline: shared state + one function per stage so the thread
// below is a flat checklist. Blocks moved verbatim, order unchanged.
struct InitCtx {
    HMODULE self = nullptr;
    std::string exepath; // empty when GetModuleFileNameA returned nothing
    std::string exe_base; // empty when no exe module / headers unreadable
    std::string gamedir;
    std::string logpath;
    std::string prof;
    const char* game = "unknown";
    int season = 0;
    ttmod::ModState state;
    ttmod::Discovery disc;
    ttmod::CacheSync cache;
    std::vector<ttmod_win::ScannedMod> all;
};

// Module/base diagnostic: never assume 0x400000 — read the headers.
static void survey_exe_base(InitCtx& ctx) {
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
    if (base[0]) ctx.exe_base = base;
}

static void ensure_dirs(InitCtx& ctx) {
    char dll[MAX_PATH] = {};
    GetModuleFileNameA(ctx.self, dll, MAX_PATH);
    char ep[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, ep, MAX_PATH);
    ctx.exepath = ep;
    ctx.gamedir = ttmod_win::dir_of(ctx.exepath.empty() ? "." : ctx.exepath.c_str());
    // M11 layout: user dirs are siblings; framework creates them on first run.
    CreateDirectoryA(ttmod_win::join(ctx.gamedir, "mods").c_str(), nullptr);
    CreateDirectoryA(ttmod_win::join(ctx.gamedir, "config").c_str(), nullptr);
    CreateDirectoryA(ttmod_win::join(ctx.gamedir, "logs").c_str(), nullptr);
    CreateDirectoryA(ttmod_win::join(ctx.gamedir, "ttmod").c_str(), nullptr);
    CreateDirectoryA(ttmod_win::join(ctx.gamedir, "ttmod\\cache").c_str(), nullptr);
    ctx.logpath = ttmod_win::join(ctx.gamedir, "logs\\ttmod.log");
    std::string exlog = ttmod_win::join(ctx.gamedir, "logs\\ttmod.exit.log");
    strncpy(g_exitlog, exlog.c_str(), sizeof g_exitlog - 1);
}

// M6 §6: loaded-module survey (env-gated). Answers "is Lua in a DLL?".
static void module_survey(const InitCtx& ctx) {
    char modlist[32] = {};
    if (GetEnvironmentVariableA("TTMOD_MODLIST", modlist, sizeof modlist) > 0) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 me;
            me.dwSize = sizeof me;
            ttmod::Logger lg;
            if (lg.open(ctx.logpath) && Module32First(snap, &me)) {
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
}

static void detect_profile(InitCtx& ctx) {
    ctx.prof = ttmod::init_from_exe(ctx.exepath.empty() ? "unknown" : ctx.exepath.c_str(),
                                    ctx.logpath);
    if (!ctx.exe_base.empty()) {
        ttmod::Logger log;
        if (log.open(ctx.logpath)) log.info(ctx.exe_base);
    }
}

static void init_events_hooks_lua(const InitCtx& ctx) {
    ttmod_win::events_init(ctx.logpath.c_str());
    ttmod_win::hooks_init(ctx.prof.c_str(), ctx.logpath.c_str());
    ttmod_win::lua_bridge_init(ctx.prof.c_str(), ctx.logpath.c_str());
    ttmod_win::stage(ctx.logpath.c_str(), "hooks+bridge init returned");
}

static void read_modstate(InitCtx& ctx) {
    if (ctx.prof == "mcsm1_pc_x86") { ctx.game = "minecraft-story-mode"; ctx.season = 1; }

    // M11 canonical discovery: game-root/mods (.ttmod + unpacked dirs).
    {
        std::string sp = ttmod_win::join(ctx.gamedir, "config\\mods.json");
        FILE* f = fopen(sp.c_str(), "rb");
        if (f) {
            std::string t;
            char b[1024];
            size_t r;
            while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
            fclose(f);
            ttmod::StateFile sf = ttmod::parse_state(t);
            if (sf.ok) ctx.state = sf.state;
        }
    }
}

static void discover_mods(InitCtx& ctx) {
    ctx.disc = ttmod::discover_mods(ttmod_win::join(ctx.gamedir, "mods"), ctx.state, ctx.game, ctx.season);
}

static void safe_mode_gate(InitCtx& ctx) {
    // M24 safe mode: env var or config/safe-mode file disables ALL
    // third-party mods (framework hooks stay; game always continues).
    char safeenv[8] = {};
    bool safe = GetEnvironmentVariableA("TTMOD_SAFE_MODE", safeenv, sizeof safeenv) > 0;
    if (!safe) {
        DWORD sa = GetFileAttributesA(ttmod_win::join(ctx.gamedir, "config\\safe-mode").c_str());
        safe = sa != INVALID_FILE_ATTRIBUTES;
    }
    if (safe) {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath)) lg.info("[TTMod] SAFE MODE: all third-party mods disabled");
        ctx.disc.mods.clear();
        ctx.disc.skipped.push_back("all: safe mode");
    }
    {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath)) {
            char h[160];
            int pkgs = 0;
            for (auto& m : ctx.disc.mods)
                if (m.packaged) pkgs++;
            snprintf(h, sizeof h, "[TTMod] Scanning mods/ (%d entries)", ctx.disc.entries_seen);
            lg.info(h);
            for (auto& s : ctx.disc.skipped) lg.info(std::string("[TTMod] Skipped: ") + s);
            // Disabled is rendered from structs, never re-parsed from log
            // text: discovery populates disc.disabled via effective_enabled().
            size_t disabled = ctx.disc.disabled.size();
            snprintf(h, sizeof h, "[TTMod] Valid mods: %u (%d packaged) Disabled: %u Skipped: %u",
                     (unsigned)ctx.disc.mods.size(), pkgs, (unsigned)disabled,
                     (unsigned)ctx.disc.skipped.size());
            lg.info(h);
        }
    }
}

static void sync_cache(InitCtx& ctx) {
    // Packaged mods extract to ttmod/cache/<id> (stale entries cleaned).
    std::vector<std::pair<std::string, std::string>> pkgs;
    for (auto& m : ctx.disc.mods)
        if (m.packaged) pkgs.emplace_back(m.id, m.source);
    ctx.cache =
        ttmod::sync_package_cache(ttmod_win::join(ctx.gamedir, "ttmod\\cache"), pkgs);
    {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath))
            for (auto& l : ctx.cache.log) lg.info(std::string("[TTMod] cache: ") + l);
    }
}

static void build_scanned(InitCtx& ctx) {
    // Uniform entries for both loaders (cache dirs for packages).
    for (auto& m : ctx.disc.mods) {
        std::string dir = m.source;
        if (m.packaged) {
            auto it = ctx.cache.effective.find(m.id);
            if (it == ctx.cache.effective.end()) continue; // sync failed, logged
            dir = it->second;
        }
        std::string prel = m.manifest.plugin.empty() ? "plugin.dll" : m.manifest.plugin;
        std::string winrel = ttmod_win::to_win(prel);
        DWORD a = GetFileAttributesA(ttmod_win::join(dir, winrel).c_str());
        bool has_dll = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        if (!m.manifest.plugin.empty() && !has_dll) {
            ttmod::Logger lg;
            if (lg.open(ctx.logpath))
                lg.info("[TTMod] " + m.id + ": declared plugin missing: " + m.manifest.plugin);
        } else if (m.manifest.plugin.empty() && !has_dll) {
            // A DLL sitting in plugins/ (or anywhere else) that the manifest
            // does not point at is SILENTLY ignored: the mod loads as
            // resource-only and its plugin never runs. That cost a day of
            // "the mod does nothing" (2026-10-02, menu.theme). Say so.
            DWORD sub = GetFileAttributesA(ttmod_win::join(dir, "plugins").c_str());
            if (sub != INVALID_FILE_ATTRIBUTES && (sub & FILE_ATTRIBUTE_DIRECTORY)) {
                ttmod::Logger lg;
                if (lg.open(ctx.logpath))
                    lg.info("[TTMod] " + m.id +
                            ": plugins/ exists but the manifest declares no \"plugin\" "
                            "path, so it is ignored (add \"plugin\": \"plugins/<name>.dll\")");
            }
        }
        ctx.all.push_back({dir, has_dll, m.packaged, has_dll ? prel : "", m.manifest});
    }
    std::sort(ctx.all.begin(), ctx.all.end(), [](const ttmod_win::ScannedMod& a, const ttmod_win::ScannedMod& b) {
        return a.manifest.id < b.manifest.id;
    });
}

static void init_plugins(const InitCtx& ctx) {
    ttmod_win::plugins_init(ctx.all, ctx.prof.c_str(), ctx.game, ctx.season, ctx.logpath.c_str());
}

static void init_mods(const InitCtx& ctx) {
    ttmod_win::mods_init(ctx.all, ctx.gamedir.c_str(), ctx.logpath.c_str());
}

static void store_menu_snapshot(const InitCtx& ctx) {
    ttmod_win::mods_store_menu(ctx.all, ctx.disc.disabled);
}

static void init_menu(const InitCtx& ctx) {
    ttmod_win::menumods_init(ctx.gamedir.c_str(), ctx.logpath.c_str());
    ttmod_win::stage(ctx.logpath.c_str(), "init thread done (mods+plugins ready)");
}

static DWORD WINAPI InitThread(LPVOID self) {
    g_t0 = GetTickCount();
    ttmod_win::stage_mark_start();
    InitCtx ctx;
    ctx.self = (HMODULE)self;
    survey_exe_base(ctx);
    ensure_dirs(ctx);
    module_survey(ctx);
    detect_profile(ctx);
    init_events_hooks_lua(ctx);
    read_modstate(ctx);
    discover_mods(ctx);
    safe_mode_gate(ctx);
    sync_cache(ctx);
    build_scanned(ctx);
    init_plugins(ctx);
    init_mods(ctx);
    store_menu_snapshot(ctx);
    init_menu(ctx);
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
