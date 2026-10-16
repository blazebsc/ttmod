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
#include <map>
#include <string>
#include "ttmod/profile.hpp"
#include "ttmod/diag.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/package_policy.hpp"
#include "ttmod/uiqueue.hpp"
#include "ttmod/log.hpp"
#include "ttmod/discovery.hpp"
#include "ttmod/cache.hpp"
#include "ttmod/mod_lifecycle.hpp"
#include "ttmod/version.hpp"
#include "script/lua/lua_vm.hpp"
#include "script/luau/luau_vm.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/modplan.hpp"
#include "ttmod/runtime_owner.hpp"
#include "events.hpp"
#include "hooks.hpp"
#include "lua_bridge.hpp"
#include "ttmod/lua_bridge.hpp"
#include "ttmod/lua_bridge.hpp"
#include "menu_bridge.hpp"
#include "mods.hpp"
#include "stage.hpp"
#include "modscan.hpp"
#include "plugins.hpp"
#include "win32_path.hpp"

static DWORD g_t0 = 0;
static char g_exitlog[MAX_PATH] = {};
// RuntimeOwner pointer, set by InitThread, used by hooks and loader components.
// Heap-allocated so it persists beyond InitThread; NEVER freed (loader-lock
// policy, review 2026-10-16: DETACH does not destruct it - the OS reclaims).
ttmod::RuntimeOwner* g_runtime_owner = nullptr;

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
        const BYTE* t = (const BYTE*)exe + ttmod::kLoadResourceAnchor.rva;
        n = snprintf(buf, sizeof buf, "exit: dt=%lums target=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                     (unsigned long)dt, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
    } else {
        n = snprintf(buf, sizeof buf, "exit: no exe module\r\n");
    }
    HANDLE f =
        CreateFileA(g_exitlog, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    std::string exepath;  // empty when GetModuleFileNameA returned nothing
    std::string exe_base; // empty when no exe module / headers unreadable
    std::string gamedir;
    std::string logpath;
    std::string prof;
    const char* game = "unknown";
    int season = 0;
    bool safe_mode = false;
    ttmod::ModState state;
    ttmod::Discovery disc;
    ttmod::CacheSync cache;
    // The single resolution result every loader reads.
    ttmod::ModPlan plan;
    std::vector<ttmod_win::ScannedMod> all;
};

// Module/base diagnostic: never assume 0x400000 — read the headers.
static void survey_exe_base(InitCtx& ctx) {
    HMODULE exe = GetModuleHandleA(nullptr);
    char base[128] = {};
    if (exe) {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)exe;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)exe + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                snprintf(base, sizeof base, "exe_base=%p image_size=0x%08lX", (void*)exe,
                         (unsigned long)nt->OptionalHeader.SizeOfImage);
        }
        if (!base[0]) snprintf(base, sizeof base, "exe_base=%p (headers unreadable)", (void*)exe);
    }
    if (base[0]) ctx.exe_base = base;
}

static void ensure_dirs(InitCtx& ctx) {
    ctx.exepath = ttmod_win::module_path(nullptr);
    ctx.gamedir = ttmod_win::dir_of(ctx.exepath.empty() ? "." : ctx.exepath.c_str());
    // M11 layout: user dirs are siblings; framework creates them on first run.
    CreateDirectoryW(ttmod::file_io::to_wide(ttmod_win::join(ctx.gamedir, "mods")).c_str(), nullptr);
    CreateDirectoryW(ttmod::file_io::to_wide(ttmod_win::join(ctx.gamedir, "config")).c_str(), nullptr);
    CreateDirectoryW(ttmod::file_io::to_wide(ttmod_win::join(ctx.gamedir, "logs")).c_str(), nullptr);
    CreateDirectoryW(ttmod::file_io::to_wide(ttmod_win::join(ctx.gamedir, "ttmod")).c_str(), nullptr);
    CreateDirectoryW(ttmod::file_io::to_wide(ttmod_win::join(ctx.gamedir, "ttmod\\cache")).c_str(), nullptr);
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
                    snprintf(m, sizeof m, "modlist: %p size=0x%08X %s", me.modBaseAddr, (unsigned)me.modBaseSize,
                             me.szModule);
                    lg.info(m);
                } while (Module32Next(snap, &me));
            }
            CloseHandle(snap);
        }
    }
}

static void detect_profile(InitCtx& ctx) {
    ttmod::GameProfile prof = ttmod::init_from_exe(ctx.exepath.empty() ? "unknown" : ctx.exepath.c_str(), ctx.logpath);
    ctx.prof = prof.id;
    if (!ctx.exe_base.empty()) {
        ttmod::Logger log;
        if (log.open(ctx.logpath)) log.info(ctx.exe_base);
    }
}

static void init_events_hooks_lua(const InitCtx& ctx, ttmod::RuntimeOwner* owner) {
    ttmod_win::events_init(ctx.logpath.c_str());
    ttmod_win::hooks_init(ctx.prof.c_str(), ctx.logpath.c_str());
    ttmod_win::lua_bridge_init(ctx.prof.c_str(), ctx.logpath.c_str(), owner);
    ttmod_win::stage(ctx.logpath.c_str(), "hooks+bridge init returned");
}

static void read_modstate(InitCtx& ctx) {
    if (ctx.prof == "mcsm1_pc_x86") {
        ctx.game = "minecraft-story-mode";
        ctx.season = 1;
    }

    // M11 canonical discovery: game-root/mods (.zip + unpacked dirs).
    {
        std::string sp = ttmod_win::join(ctx.gamedir, "config\\mods.json");
        FILE* f = ttmod::file_io::open_read(sp);
        if (f) {
            std::string t;
            char b[1024];
            size_t r;
            while ((r = fread(b, 1, sizeof b, f)) > 0) t.append(b, r);
            fclose(f);
            auto sf = ttmod::parse_state(t);
            if (sf.ok()) ctx.state = sf.value();
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
        // Do NOT clear disc.mods: the plan blocks them through the same
        // path a broken dependency takes, so the menu can still show what
        // is installed. Discovery output stays intact for the summary.
        ctx.safe_mode = true;
    }
    {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath)) {
            char h[160];
            int pkgs = 0;
            for (auto& m : ctx.disc.mods)
                if (m.packaged()) pkgs++;
            snprintf(h, sizeof h, "[TTMod] Scanning mods/ (%d entries)", ctx.disc.entries_seen);
            lg.info(h);
            for (auto& s : ctx.disc.skipped) lg.info(std::string("[TTMod] Skipped: ") + s);
            // Disabled is rendered from structs, never re-parsed from log
            // text: discovery populates disc.disabled via effective_enabled().
            size_t disabled = ctx.disc.disabled.size();
            snprintf(h, sizeof h, "[TTMod] Valid mods: %u (%d packaged) Disabled: %u Skipped: %u",
                     (unsigned)ctx.disc.mods.size(), pkgs, (unsigned)disabled, (unsigned)ctx.disc.skipped.size());
            lg.info(h);
        }
    }
}

static void sync_cache(InitCtx& ctx) {
    // Packaged mods extract to ttmod/cache/<id> (stale entries cleaned).
    std::vector<std::pair<ttmod::ModId, std::string>> pkgs;
    for (auto& m : ctx.disc.mods)
        if (m.packaged()) pkgs.emplace_back(m.id, m.source.path);
    auto synced = ttmod::sync_package_cache(ttmod_win::join(ctx.gamedir, "ttmod\\cache"), pkgs);
    if (synced.ok()) {
        ctx.cache = synced.value();
    } else {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath)) lg.info(std::string("[TTMod] cache: sync failed: ") + synced.error().message);
        return;
    }
    {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath))
            for (auto& l : ctx.cache.log) lg.info(std::string("[TTMod] cache: ") + l);
    }
}

static void build_plan(InitCtx& ctx) {
    // ONE resolution for the whole runtime: discovery + cache in, one
    // dependency-first plan out. The resource index, the plugin loader and
    // the menu all read this, so they cannot disagree about what loads.
    ctx.plan = ttmod::build_plan(ctx.disc, ctx.cache, ttmod::ModPlanOptions{ctx.safe_mode});
}

static void build_scanned(InitCtx& ctx) {
    // Uniform entries for both loaders, in the plan's load order (already
    // dependency-first and deterministic - no re-sorting by id here).
    for (auto& m : ctx.plan.load_order) {
        const std::string& dir = m.dir;
        std::string prel = m.manifest.plugin.path.empty() ? "plugin.dll" : m.manifest.plugin.path;
        std::string winrel = ttmod_win::to_win(prel);
        DWORD a = GetFileAttributesA(ttmod_win::join(dir, winrel).c_str());
        bool has_dll = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        if (!m.manifest.plugin.path.empty() && !has_dll) {
            ttmod::Logger lg;
            if (lg.open(ctx.logpath))
                lg.info("[TTMod] " + m.id.str() + ": declared plugin missing: " + m.manifest.plugin.path);
        } else if (m.manifest.plugin.path.empty() && !has_dll) {
            // A DLL sitting in plugins/ (or anywhere else) that the manifest
            // does not point at is SILENTLY ignored: the mod loads as
            // resource-only and its plugin never runs. That cost a day of
            // "the mod does nothing" (2026-10-02, menu.theme). Say so.
            DWORD sub = GetFileAttributesA(ttmod_win::join(dir, "plugins").c_str());
            if (sub != INVALID_FILE_ATTRIBUTES && (sub & FILE_ATTRIBUTE_DIRECTORY)) {
                ttmod::Logger lg;
                if (lg.open(ctx.logpath))
                    lg.info("[TTMod] " + m.id.str() +
                            ": plugins/ exists but the manifest declares no \"plugin\" "
                            "path, so it is ignored (add \"plugin\": \"plugins/<name>.dll\")");
            }
        }
        ctx.all.push_back({dir, has_dll, m.packaged(), has_dll ? prel : "", m.manifest});
    }
    {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath)) {
            for (auto& m : ctx.plan.blocked_mods)
                lg.info("[TTMod] " + m.id.str() + " blocked: " + ctx.plan.blocked_reason(m.id));
            for (auto& s : ctx.plan.skipped) lg.info("[TTMod] Skipped: " + s);
        }
    }
}

static void init_plugins(const InitCtx& ctx, ttmod::RuntimeOwner* owner) {
    ttmod_win::plugins_init(ctx.all, ctx.prof.c_str(), ctx.game, ctx.season, ctx.logpath.c_str(), owner);
}

// PR 32/33: bounded entrypoint reads moved to core file_io
// (read_file_bounded): distinct unreadable/oversized errors, never a
// truncated prefix (review 2026-10-16), unit-testable on Linux. Same
// manifest-class cap - third-party text ingested by the framework.
static ttmod::Result<std::string> read_entrypoint(const std::string& path) {
    return ttmod::file_io::read_file_bounded(path, (size_t)ttmod::packlimits::kMaxManifestBytes);
}

// PR 32: queue enabled mods' telltale-lua entrypoints onto the game thread
// (existing v5 queue: runs at the next script load + menu replay). ctx.all
// is the surviving ModPlan-ordered set, so safe-mode/disabled/blocked mods
// are excluded by construction - nothing extra to gate. File reads bounded
// by the manifest cap (third-party text ingested by the framework).
static void queue_telltale_entrypoints(const InitCtx& ctx) {
    std::vector<ttmod::TelltaleMod> mods;
    for (auto& s : ctx.all) {
        const auto& m = s.manifest;
        bool tt = false;
        for (auto r : m.runtime.runtimes)
            if (r == ttmod::Runtime::TelltaleLua) tt = true;
        mods.push_back({m.identity.id.str(), tt, m.entrypoints.telltale_lua, true});
    }
    ttmod::Logger lg;
    bool logopen = lg.open(ctx.logpath);
    auto emit = [&](const ttmod::DiagEvent& e) {
        if (logopen) lg.info(ttmod::diag_format(e));
    };
    for (auto& e : ttmod::collect_telltale_entrypoints(mods)) {
        std::string dir;
        for (auto& s : ctx.all)
            if (s.manifest.identity.id.str() == e.id) dir = s.dir;
        auto rd = read_entrypoint(ttmod_win::join(dir, e.entrypoint));
        if (!rd.ok()) {
            // Unreadable and oversized are distinct (review 2026-10-16): a
            // truncated script must NEVER reach the game's Lua.
            emit({ttmod::DiagKind::Bridge,
                  rd.error().category == ttmod::errcat::kLimit ? "entrypoint-oversized" : "entrypoint-unreadable",
                  e.id, e.entrypoint + ": " + rd.error().message});
            continue;
        }
        if (rd.value().empty()) {
            emit({ttmod::DiagKind::Bridge, "entrypoint-empty", e.id, e.entrypoint});
            continue;
        }
        if (!ttmod::uiqueue_push(rd.value()))
            emit({ttmod::DiagKind::Bridge, "entrypoint-dropped", e.id, e.entrypoint});
        else
            emit({ttmod::DiagKind::Bridge, "entrypoint-queued", e.id, e.entrypoint});
    }
}

// PR 33: run enabled mods' TTMod-Lua entrypoints on the shared VM (one
// environment each, PR 17 runner). Returns per-mod statuses for the menu
// model; empty when no VM (safe mode - the plan already excluded
// everything) or no lua mods. With the Null VM every lua mod records a
// Backend failure (truthful, auto-heals when a real backend lands);
// failures surface as menu warnings, never crashes.
// PR 31 wiring: the second, Luau script VM. Process-lifetime static (no
// unload path exists for anything else either - PR 21); safe mode never
// creates it (wire_script_vm guards). Null when creation fails: Luau mods
// then report Backend failures from the runner (truthful, no crashes).
static ttmod::ScriptVm* g_luau_vm = nullptr;

static std::vector<ttmod::ModRunStatus> run_ttmod_entrypoints(const InitCtx& ctx, ttmod::RuntimeOwner* owner) {
    std::vector<ttmod::ModRunStatus> none;
    ttmod::ScriptVm* vm = owner ? owner->vm() : nullptr;
    if (!vm) return none;
    // Selection through the tested collector (PR 33): one pass per script
    // runtime; a non-Lua/Luau runtime maps to a marker the collector
    // rejects. A mod may declare BOTH script runtimes (review 2026-10-16:
    // the old loop broke at the first match and silently dropped the other
    // entrypoint) - one candidate PER declared runtime, each carrying its
    // own entrypoint; the per-runtime collect passes below pick their own.
    std::vector<ttmod::ScriptModEntry> cands;
    std::map<std::string, std::string> dirs;
    std::map<std::string, std::vector<ttmod::Permission>> perms; // declared (PR 17 filtering)
    for (auto& s : ctx.all) {
        const auto& m = s.manifest;
        bool any_script = false;
        for (auto r : m.runtime.runtimes) {
            if (r == ttmod::Runtime::Lua && !m.entrypoints.lua.empty()) {
                cands.push_back({m.identity.id.str(), r, m.entrypoints.lua, true});
                any_script = true;
            } else if (r == ttmod::Runtime::Luau && !m.entrypoints.luau.empty()) {
                cands.push_back({m.identity.id.str(), r, m.entrypoints.luau, true});
                any_script = true;
            }
        }
        if (!any_script)
            cands.push_back({m.identity.id.str(), ttmod::Runtime::TelltaleLua, "", true}); // collector rejects
        dirs[m.identity.id.str()] = s.dir;
        perms[m.identity.id.str()] = m.runtime.permissions;
    }
    // Lua mods on the owner VM (created first); Luau mods on the Luau VM.
    // No VM for a runtime = every selected mod records a Backend failure
    // with the runner's matching message (no silent skips).
    std::vector<ttmod::ModRunStatus> statuses;
    // Entrypoint read: unreadable/oversized -> Entrypoint-stage failure with
    // the reader's exact error; empty file -> the runner's empty-chunk
    // Entrypoint failure. Never a truncated prefix (review 2026-10-16).
    auto fail_read = [&](const ttmod::SelectedEntrypoint& s, const ttmod::Error& e) {
        statuses.push_back(ttmod::ModRunStatus{
            s.id, ttmod::ModRunState::Failed,
            {ttmod::ModStage::Entrypoint, e}, 1});
    };
    std::vector<ttmod::ModScript> lua_items;
    for (auto& s : ttmod::collect_script_entrypoints(cands, ttmod::Runtime::Lua)) {
        auto rd = read_entrypoint(ttmod_win::join(dirs[s.id], s.entrypoint));
        if (!rd.ok()) {
            fail_read(s, rd.error());
            continue;
        }
        lua_items.push_back({s.id, ttmod::Runtime::Lua, s.entrypoint, std::move(rd.value()),
                             perms.count(s.id) ? perms[s.id] : std::vector<ttmod::Permission>{}});
    }
    if (!lua_items.empty()) {
        auto st = ttmod::run_mod_entrypoints(*vm, lua_items);
        statuses.insert(statuses.end(), st.begin(), st.end());
    }
    std::vector<ttmod::ModScript> luau_items;
    for (auto& s : ttmod::collect_script_entrypoints(cands, ttmod::Runtime::Luau)) {
        auto rd = read_entrypoint(ttmod_win::join(dirs[s.id], s.entrypoint));
        if (!rd.ok()) {
            fail_read(s, rd.error());
            continue;
        }
        luau_items.push_back({s.id, ttmod::Runtime::Luau, s.entrypoint, std::move(rd.value()),
                              perms.count(s.id) ? perms[s.id] : std::vector<ttmod::Permission>{}});
    }
    if (!luau_items.empty()) {
        if (g_luau_vm != nullptr) {
            auto st = ttmod::run_mod_entrypoints(*g_luau_vm, luau_items);
            statuses.insert(statuses.end(), st.begin(), st.end());
        } else {
            for (auto& i : luau_items)
                statuses.push_back(ttmod::ModRunStatus{
                    i.id, ttmod::ModRunState::Failed,
                    {ttmod::ModStage::Backend,
                     {"run-entrypoint", i.chunk_name, ttmod::errcat::kType, "no Luau VM (create failed)"}},
                    1});
        }
    }
    if (statuses.empty()) return none;
    ttmod::Logger lg;
    if (lg.open(ctx.logpath)) {
        char m[128];
        size_t failed = 0;
        for (auto& s : statuses)
            if (s.state == ttmod::ModRunState::Failed) {
                ++failed;
                lg.info("scripts: " + s.id + " failed: " + s.last_error_line());
            }
        snprintf(m, sizeof m, "scripts: %u ran, %u failed", (unsigned)statuses.size(), (unsigned)failed);
        lg.info(m);
    }
    return statuses;
}

static void init_mods(const InitCtx& ctx, ttmod::RuntimeOwner* owner) {
    ttmod_win::mods_init(ctx.all, ctx.gamedir.c_str(), ctx.logpath.c_str(), owner);
}

static void store_menu_snapshot(const InitCtx& ctx, const std::vector<ttmod::ModRunStatus>& scripts) {
    ttmod_win::mods_store_menu(ctx.all, ctx.disc.disabled, ctx.plan, scripts);
}

static void init_menu(const InitCtx& ctx, ttmod::RuntimeOwner* owner) {
    ttmod_win::menumods_init(ctx.gamedir.c_str(), ctx.logpath.c_str(), owner);
    ttmod_win::stage(ctx.logpath.c_str(), "init thread done (mods+plugins ready)");
}

// Production script bindings (PR 30 wiring): the minimal host surface —
// ttmod.log.info(msg) — so a script mod can prove it ran and talk to the
// same log everything else does. The fuller ttmod.* surface is planned;
// this is the one binding verification actually needs.
static std::string g_script_logpath;
static ttmod::Result<ttmod::Value> script_log_info(ttmod::CallCtx& c) {
    ttmod::Logger lg;
    if (lg.open(g_script_logpath)) lg.info("scripts: " + c.arg_str(0));
    return ttmod::Result<ttmod::Value>::ok(ttmod::Value::nil());
}
// ttmod.version: read-only, ungated (knowing the host version harms nothing).
static ttmod::Result<ttmod::Value> script_version(ttmod::CallCtx&) {
    return ttmod::Result<ttmod::Value>::ok(ttmod::Value::string(ttmod::kVersion));
}
// ttmod.mods.list: gated on the CALLING MOD's declared mods.read (PR 17
// capability filtering - undeclared -> structured refusal). Reads the menu
// store, the same source of truth the in-game list renders.
static ttmod::Result<ttmod::Value> script_mods_list(ttmod::CallCtx&) {
    if (!ttmod::caller_permitted(ttmod::Permission::ModsRead))
        return ttmod::Result<ttmod::Value>::fail(
            ttmod::Error{"mods", "list", ttmod::errcat::kType, "mods.read not granted"});
    auto items = std::make_shared<ttmod::Value::ArrayVec>();
    int n = ttmod_win::mods_menu_count();
    for (int i = 0; i < n; ++i) {
        ttmod_modinfo mi{};
        if (ttmod_win::mods_menu_info(i, &mi) != 0) continue;
        auto fields = std::make_shared<ttmod::Value::Fields>();
        fields->emplace_back("id", ttmod::Value::string(mi.id));
        fields->emplace_back("version", ttmod::Value::string(mi.version));
        fields->emplace_back("enabled", ttmod::Value::boolean(mi.enabled != 0));
        fields->emplace_back("has_plugin", ttmod::Value::boolean(mi.has_plugin != 0));
        fields->emplace_back("packaged", ttmod::Value::boolean(mi.packaged != 0));
        items->push_back(ttmod::Value::object(fields));
    }
    return ttmod::Result<ttmod::Value>::ok(ttmod::Value::array(items));
}
static const ttmod::BindingDesc kScriptBindings[] = {
    {"log", "info", &script_log_info, 1, 1},
    {"version", "string", &script_version, 0, 0},
    {"mods", "list", &script_mods_list, 0, 0},
};

// PR 30 wiring: replace the owner's Null placeholder with a real Lua VM
// (TTMod-owned, never the game's states). Lua mods then run at init via
// run_ttmod_entrypoints with per-mod envs; failures surface as menu
// warnings. Luau mods run on the Luau VM (same wiring; the library is
// linked since PR 31's completion - see docs/runtime/luau.md).
// Safe mode never reaches here (prepare returns false and vm() is null).
static void wire_script_vm(const InitCtx& ctx, ttmod::RuntimeOwner* owner) {
    if (owner->vm() == nullptr) return; // safe mode / prepare failed: Null-less is fine
    g_script_logpath = ctx.logpath;
    // Memory caps (both VMs): 256MB default - generous for mods, bounded
    // for the game process. Instruction budget stays the backend default.
    ttmod::LuaVmOptions lopt;
    lopt.memory_budget_mb = 256;
    auto vm = ttmod::make_lua_vm(owner->api_registry(), nullptr,
                                 std::span<const ttmod::BindingDesc>(kScriptBindings), lopt);
    if (!vm.ok()) {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath))
            lg.info("[TTMod] script VM create failed, mods will report backend failures: " +
                    vm.error().message);
        return; // keep the Null VM: structured failures, never a crash
    }
    owner->set_vm(std::move(vm.value()));
    ttmod::Logger lg;
    bool logopen = lg.open(ctx.logpath);
    if (logopen) lg.info("[TTMod] script VM live (lua 5.4, ttmod.log.info bound)");
    // PR 31 wiring: the Luau VM - same registry (plugin bindings install on
    // both via plugins_set_luau_vm), same bindings table, process-lifetime.
    // Creation failure is logged and leaves g_luau_vm null: Luau mods then
    // record Backend failures (truthful, the Lua path is unaffected).
    ttmod::LuauVmOptions uopt;
    uopt.memory_budget_mb = 256;
    auto luau = ttmod::make_luau_vm(owner->api_registry(), nullptr,
                                    std::span<const ttmod::BindingDesc>(kScriptBindings), uopt);
    if (luau.ok()) {
        static std::unique_ptr<ttmod::ScriptVm> luau_holder; // process lifetime, like the owner
        luau_holder = std::move(luau.value());
        g_luau_vm = luau_holder.get();
        ttmod_win::plugins_set_luau_vm(g_luau_vm);
        if (logopen) lg.info("[TTMod] luau script VM live");
    } else if (logopen) {
        lg.info("[TTMod] luau VM create failed, luau mods report backend failures: " + luau.error().message);
    }
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

    // Hooks + Lua bridge FIRST (v0.12.0 order): the LoadResource detour must
    // be live before plugins queue UI chunks, because the hook is what runs
    // them. A RuntimeOwner is created before the hooks so hooks can observe
    // states through it; it is prepared (non-blocking) after the plan exists.
    ttmod::RuntimeOwner* owner = new ttmod::RuntimeOwner(ctx.prof.c_str(), ctx.logpath.c_str());
    g_runtime_owner = owner; // hooks see it from the first callback
    init_events_hooks_lua(ctx, owner);

    read_modstate(ctx);
    discover_mods(ctx);
    safe_mode_gate(ctx);
    sync_cache(ctx);
    build_plan(ctx);

    if (!owner->prepare_runtime(ctx.plan)) {
        ttmod::Logger lg;
        if (lg.open(ctx.logpath)) lg.info("[TTMod] RuntimeOwner prepare failed, continuing without script runtime");
    }

    build_scanned(ctx);
    wire_script_vm(ctx, owner);
    init_plugins(ctx, owner);
    queue_telltale_entrypoints(ctx);
    init_mods(ctx, owner);
    // Menu store BEFORE script entrypoints (review 2026-10-16): the store is
    // what ttmod.mods.list() reads, so populating it after the runner meant
    // scripts saw an EMPTY list at startup. Script statuses attach after the
    // run, keeping the menu's Blocked rows without re-storing everything.
    store_menu_snapshot(ctx, {});
    auto script_status = run_ttmod_entrypoints(ctx, owner);
    ttmod_win::mods_store_script_warnings(script_status);
    init_menu(ctx, owner);

    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        HANDLE t = CreateThread(nullptr, 0, InitThread, self, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        // LOADER-LOCK POLICY (review 2026-10-16): DETACH runs under the OS
        // loader lock, with other threads still running. Complex teardown
        // there (owner shutdown: destroying mutexes, closing the script
        // VMs, stopping the dispatcher) can deadlock or race - and is
        // pointless at process exit, where the OS reclaims everything. We
        // do not support unloading the framework from a live process (PR
        // 21 policy), so detach does the one safe thing: arm the dead flag
        // (every hook becomes a no-op) and write the exit dump.
        ttmod_win::lua_bridge_shutdown();
        exit_dump();
    }
    return TRUE; // never block the game
}
#endif
