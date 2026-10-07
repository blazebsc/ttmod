// See lua_bridge.hpp for the design contract.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"
#include "win32_path.hpp"
#include "ttmod/game_lua.hpp"
#include "ttmod/log.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/runtime.hpp"
#include "ttmod/theme_color.hpp"
#include "ttmod/lua_bridge.hpp"
#include "ttmod/mcsm1_addrs.hpp"
#include "ttmod/uiqueue.hpp"
#include "lua_abi.hpp"
#include "lua_bridge.hpp"
#include "menu_bridge.hpp"
#include "stage.hpp"
#include "ttmod/runtime_owner.hpp"
#include "ttmod/script_vm.hpp"
#include "ttmod/script_api.hpp"
#include "../../../core/script/lua/lua_vm.hpp"

// RuntimeOwner created in InitThread, used by hooks and loader components.
extern ttmod::RuntimeOwner* g_runtime_owner;

// Static flag to track if real VM was created
static bool g_vm_created = false;

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
// Game Lua state registry (see LuaStateInfo below): the game owns every
// state; g_state/g_states_seen removed in favor of identity + role.
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

// Game Lua state registry: identity + role + lifecycle (ADR-006, Step 8).
// The rules live in core (ttmod::GameLuaRegistry) where they are unit
// tested; this layer only supplies the opaque handle and does the logging.
// The game owns every state - we observe, never own or close.
static ttmod::GameLuaRegistry g_states;

static void note_script_on_state(lua_State* L, const char* filename) {
    if (!L || !filename) return;
    // The core call locks internally and returns only a bool, so file I/O
    // (emit) stays outside the critical section: logging under a registry
    // mutex would serialize unrelated LoadResource threads behind disk.
    if (!g_states.note_script(L, filename)) return;
    int order = 0;
    for (auto& e : g_states.entries())
        if (e.handle == (void*)L) {
            order = e.order;
            break;
        }
    char m[128];
    snprintf(m, sizeof m, "lua: state #%d identified as %s (%s)", order,
             ttmod::to_string(ttmod::GameLuaRegistry::role_for_script(filename)), filename);
    emit(m);
}

// Native color-setter hook addresses. The UI region unpacks progressively,
// so hook_loadresource starts the bounded retry thread below instead of
// installing in the late-hook window.
static BYTE* g_aspbase = nullptr;
using ScolFn = int(__attribute__((thiscall)) *)(void*, void*, void*, int);
static ScolFn g_origScol = nullptr;
static int __attribute__((thiscall)) hook_scol(void* self, void* desc, void* color, int flag);
static bool scol_accent(float* out);

// Bounded retry for the UI-region hook: that memory unpacks progressively,
// so the anchor may take up to a minute to match. Gives up loudly.
struct UiProbeTarget {
    const char* tag;
    uint32_t rva;
    uint8_t anchor[10];
    size_t alen;
    LPVOID detour;
    LPVOID* origstore;
    volatile LONG hits;
    int cap;
    bool done;
};
static UiProbeTarget g_uiprobes[] = {
    {"scol",
     ttmod::kScolRva,
     {0x55, 0x8B, 0xEC, 0x51, 0x56, 0x57, 0x8B, 0xF1, 0xE8, 0xD3},
     10,
     (LPVOID)hook_scol,
     (LPVOID*)&g_origScol,
     0,
     5000,
     false},
};
static DWORD WINAPI ui_probe_thread(LPVOID) {
    int remaining = 1;
    for (size_t k = 0; k < sizeof g_uiprobes / sizeof g_uiprobes[0]; ++k)
        if (g_uiprobes[k].done) --remaining;
    for (int i = 0; i < 30 && !g_dead && remaining > 0; ++i) {
        Sleep(2000);
        if (g_dead) return 0;
        BYTE* rbase = (BYTE*)GetModuleHandleA(nullptr);
        if (!rbase) continue;
        for (size_t k = 0; k < sizeof g_uiprobes / sizeof g_uiprobes[0]; ++k) {
            UiProbeTarget& t = g_uiprobes[k];
            if (t.done) continue;
            if (memcmp(rbase + t.rva, t.anchor, t.alen) != 0) continue;
            g_aspbase = rbase;
            void* tgt = (void*)(rbase + t.rva);
            if (MH_CreateHook(tgt, t.detour, t.origstore) == MH_OK && MH_EnableHook(tgt) == MH_OK) {
                char m[128];
                snprintf(m, sizeof m, "%s: hook installed (anchor-verified, retry)", t.tag);
                emit(m);
            } else {
                char m[128];
                snprintf(m, sizeof m, "%s: MH_CreateHook failed, will not retry", t.tag);
                emit(m);
            }
            t.done = true;
            --remaining;
        }
    }
    for (size_t k = 0; k < sizeof g_uiprobes / sizeof g_uiprobes[0]; ++k) {
        if (!g_uiprobes[k].done) {
            char m[128];
            snprintf(m, sizeof m, "%s: anchor never matched (60s), giving up", g_uiprobes[k].tag);
            emit(m);
            g_uiprobes[k].done = true;
        }
    }
    return 0;
}

// Append-report sink for the Menu_Main wrapper (same-thread, never errors).
static int __cdecl append_log(lua_State* L) {
    const char* s = g_fnTolstring ? g_fnTolstring(L, 1, nullptr) : nullptr;
    char m[160];
    snprintf(m, sizeof m, "lua-probe: %p %s", (void*)L, s ? s : "?");
    emit(m);
    return 0;
}

// One-shot UNPACKED-MEMORY DUMP (TTMOD_DUMP_MEM=<path>, research tool).
// The exe is packed: file bytes differ from live code, so static analysis of
// the on-disk exe only sees the packer. This dumps the live module image at
// the moment Menu.lua loads - by then the engine has unpacked and is running
// its menu/UI code, which is the region we need to analyse (the hover
// highlight is engine-internal rendering; no settable property exists, so a
// native hook is the only route). Dumped once per process, then the flag is
// inert. The dump is game-derived: keep it outside the repo (policy).
static volatile LONG g_dump_done = 0;
static void dump_module_image(const char* out_path) {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe || out_path == nullptr || !out_path[0]) return;
    BYTE* base = (BYTE*)exe;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        emit("dump: bad DOS signature, aborting");
        return;
    }
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        emit("dump: bad NT signature, aborting");
        return;
    }
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    FILE* f = ttmod::file_io::open_write(out_path);
    if (!f) {
        emit("dump: cannot open output file");
        return;
    }
    // Header + every mapped section, each prefixed with its file offset so the
    // dump can be re-assembled at the right RVAs for static analysis.
    fwrite(base, 1, nt->OptionalHeader.SizeOfHeaders, f);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (sec->SizeOfRawData == 0) continue;
        // the image is mapped: VirtualAddress IS the file offset in the dump
        fseek(f, sec->PointerToRawData ? sec->PointerToRawData : sec->VirtualAddress, SEEK_SET);
        fwrite(base + sec->VirtualAddress, 1, sec->SizeOfRawData, f);
    }
    fclose(f);
    char m[160];
    snprintf(m, sizeof m, "dump: wrote unpacked image (%u sections) to %s", nt->FileHeader.NumberOfSections, out_path);
    emit(m);
}

static int __cdecl hook_loadresource(lua_State* L, char* filename) {
    int rc = g_origLoadResource(L, filename);
    if (!L || g_dead) return rc;
    if (InterlockedIncrement(&g_loadlog) <= 5) {
        char m[160];
        snprintf(m, sizeof m, "lua: loadresource #%ld %s", (long)g_loadlog, filename ? filename : "?");
        emit(m);
    }
    // Observe state and classify role via RuntimeOwner
    if (g_runtime_owner) {
        g_runtime_owner->runtime().observe_state(L, filename);
        // Upgrade from Null VM to real Lua VM if states are ready and not yet created
        if (!g_vm_created && g_runtime_owner->runtime().is_ready()) {
            auto created = make_lua_vm(g_runtime_owner->api_registry(),
                                       g_runtime_owner,
                                       std::span<const ttmod::BindingDesc>(),
                                       ttmod::LuaVmOptions{});
            if (created.ok()) {
                g_runtime_owner->set_vm(std::move(created.value()));
                g_vm_created = true;
                emit("lua: real VM created and installed");
            } else {
                emit("lua: failed to create real VM");
            }
        }
    } else {
        note_script_on_state(L, filename); // fallback
    }
    // Drain plugin chunks and other dispatcher work via RuntimeOwner's dispatcher
    if (g_runtime_owner) {
        g_runtime_owner->dispatcher().pump();
    } else {
        // Fallback to old uiqueue for compatibility
        for (const std::string& c : ttmod::uiqueue_take())
            bridge_run_chunk(L, g_fnLoadstring, g_fnPcallk, g_fnGettop, g_fnSetglobal, g_fnTolstring, "plugin",
                             c.c_str());
    }
    // Offer Menu_Add wrapper once when Menu.lua loads
    if (g_runtime_owner && filename && tail_matches(filename, "Menu.lua")) {
        g_runtime_owner->runtime().maybe_install_menu_add_wrapper(L);
    }
    return rc;
}

// (2026-10-05 cleanup) Retired diagnosis detours - asp (binding probe),
// rol (dead binding), scolB (getter: substitution was a no-op), uifx
// (flag means click-armed, not just highlighted) - lived here. What remains
// is the one load-bearing hook below: scol color substitution.

// Native color-setter probes (log-only). 0x568430 / 0x5684B0 are the engine
// setters the Rollover binding calls: thiscall (ecx=this, ret $0xc), args
// (desc, colorStruct*, flag). The detour logs the 4 floats + caller so the
// hover writer identifies itself; MinGW thiscall forwards ecx untouched, so
// the trampoline sees the original register state either way.
static void scol_log(const char* tag, volatile LONG* hits, int cap, void* color) {
    LONG n = InterlockedIncrement(hits);
    if (n <= cap && g_aspbase && color) {
        float* c = (float*)color;
        void* ret = __builtin_return_address(0);
        char m[192];
        snprintf(m, sizeof m, "%s: #%ld t=%lu c=%.3f,%.3f,%.3f,%.3f caller=%08X", tag, (long)n,
                 (unsigned long)GetTickCount(), c[0], c[1], c[2], c[3], (unsigned)((BYTE*)ret - g_aspbase));
        emit(m);
    }
}
// Accent source for scolB substitution: the same file the menu-theme mod
// reads (config/menu.theme.json {"accent": "#RRGGBB"}). Missing/invalid =
// passthrough. Read per substitution (hover-rate, tiny file, no cache).
static bool scol_accent(float* out) {
    // Cached per process: config changes need restart everywhere else too.
    // (Hover/init bursts fire thousands of sets per second; file I/O per
    // set would hitch menu builds.)
    static bool cached = false;
    static bool have = false;
    static float rgb[3] = {};
    if (cached) {
        if (!have) return false;
        out[0] = rgb[0];
        out[1] = rgb[1];
        out[2] = rgb[2];
        return true;
    }
    cached = true;
    std::string exe = ttmod_win::module_path(nullptr);
    if (exe.empty()) return false;
    size_t slash = exe.find_last_of("\\/");
    std::string root = slash == std::string::npos ? "." : exe.substr(0, slash);
    std::string path = root + "\\config\\menu.theme.json";
    FILE* f = ttmod::file_io::open_read(path);
    if (!f) return false;
    char t[256] = {};
    size_t r = fread(t, 1, sizeof t - 1, f);
    fclose(f);
    if (r == 0) return false;
    const char* k = strstr(t, "\"accent\"");
    if (!k) return false;
    const char* q = strchr(k + 8, '"');
    if (!q || q[1] != '#') return false;
    char hex[8] = {};
    strncpy(hex, q + 1, 7);
    auto parsed = ttmod::parse_accent(hex);
    if (!parsed.ok()) {
        emit(std::string("theme: invalid accent: ") + parsed.error().message);
        return false;
    }
    out[0] = parsed.value().r;
    out[1] = parsed.value().g;
    out[2] = parsed.value().b;
    rgb[0] = out[0];
    rgb[1] = out[1];
    rgb[2] = out[2];
    // Honor mod disable: substitution only runs while menu.theme is enabled
    // in config/mods.json (2026-10-05: it fired with the mod disabled,
    // contaminating a stock-behavior test). Missing/unparseable = off.
    {
        std::string mp = root + "\\config\\mods.json";
        FILE* mf = ttmod::file_io::open_read(mp);
        if (!mf) return false;
        char mt[512] = {};
        size_t mr = fread(mt, 1, sizeof mt - 1, mf);
        fclose(mf);
        if (mr == 0) return false;
        const char* id = strstr(mt, "menu.theme");
        if (!id || id - mt + 200 > (ptrdiff_t)sizeof mt) return false;
        const char* en = strstr(id, "\"enabled\"");
        if (!en || en - id > 200) return false;
        const char* colon = strchr(en + 9, ':');
        if (!colon) return false;
        const char* p = colon + 1;
        while (*p == ' ' || *p == '\t') ++p;
        if (strncmp(p, "true", 4) != 0) return false;
    }
    have = true;
    return true;
}
static volatile LONG g_scolBsub = 0;
static int __attribute__((thiscall)) hook_scol(void* self, void* desc, void* color, int flag) {
    // Substitution (2026-10-05): REA proved 0x5684B0 (scolB) is a property
    // GETTER - its "substitute" edited a buffer the getter overwrites, i.e.
    // it never did anything. THIS setter (0x568430) is the real write path:
    // near-gray-bright color structs become the accent. Broad by necessity:
    // descriptors are per-agent-instance (agent+0x58 per the binding
    // callsite), so identity learning would churn every menu rebuild.
    // Scope instead by value shape (gray+bright only; black, disabled gray,
    // real tints pass) + theme gate (config accent + mod enabled).
    // scolB stays as the traffic witness.
    float* c = (float*)color;
    if (color && desc) {
        float acc[3] = {};
        bool haveAcc = scol_accent(acc);
        bool sub = haveAcc && ttmod::should_substitute(c[0], c[1], c[2]);
        if (sub) {
            LONG n = InterlockedIncrement(&g_scolBsub);
            if (n <= 400) {
                char m[128];
                snprintf(m, sizeof m, "scol-sub: %.3f,%.3f,%.3f -> accent", c[0], c[1], c[2]);
                emit(m);
            }
            c[0] = acc[0];
            c[1] = acc[1];
            c[2] = acc[2];
        } else if (haveAcc && !(c[0] <= 0.001f && c[1] <= 0.001f && c[2] <= 0.001f)) {
            scol_log("scol", &g_uiprobes[0].hits, g_uiprobes[0].cap, color);
        } else {
            InterlockedIncrement(&g_uiprobes[0].hits);
        }
    }
    if (g_origScol) return g_origScol(self, desc, color, flag);
    return 0;
}

static lua_State* __cdecl hook_lua_newstate(lua_Alloc alloc, void* ud) {
    lua_State* L = g_origNewstate(alloc, ud);
    if (!L || g_dead) return L;
    {
        int order = g_states.observe(L);
        if (order == 1) {
            emit("lua: lua_newstate observed, live state captured");
            stage(g_logpath.c_str(), "lua_newstate observed");
        }
    }
    // One-shot proof on the fresh state, on this same game thread.
    if (InterlockedCompareExchange(&g_test_done, 1, 0) == 0 && g_fnLoadstring && g_fnPcallk && g_fnGettop) {
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
            snprintf(m, sizeof m, "lua: bridge test FAILED load=%d pcall=%d top=%d->%d err=%s", lr, pr, t0, t1,
                     err ? err : "?");
            emit(m);
        }
    }
    // Mods-menu UI: idempotent defs on EVERY captured state so whichever
    // state hosts the menu gets the entry points + screens. Same-thread,
    // balanced-stack rules as the proof above.
    menumods_register(L, g_fnLoadstring, g_fnPcallk, g_fnGettop, g_fnTolstring, g_fnPushCClosure, g_fnSetglobal);
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
        if (memcmp(base + ttmod::kLuaNewstateAnchor.rva, ttmod::kLuaNewstateAnchor.bytes, 4) == 0) {
            ok = true;
            break;
        }
        char m[96];
        snprintf(m, sizeof m, "lua: anchor try%d bytes=%02X %02X %02X %02X", i, base[0x611C80], base[0x611C81],
                 base[0x611C82], base[0x611C83]);
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
    if (memcmp(base + ttmod::kLoadResourceLiveAnchor.rva, ttmod::kLoadResourceLiveAnchor.bytes, 4) == 0) {
        void* lr = (void*)(base + 0x1139F0);
        if (MH_CreateHook(lr, (LPVOID)hook_loadresource, (LPVOID*)&g_origLoadResource) == MH_OK &&
            MH_EnableHook(lr) == MH_OK) {
            emit("lua: loadresource hook installed (late, anchor-verified)");
        } else {
            emit("lua: MH_CreateHook(loadresource) failed, continuing without it");
        }
    } else {
        char m[96];
        snprintf(m, sizeof m, "lua: loadresource anchor mismatch %02X %02X %02X %02X, skipping", base[0x1139F0],
                 base[0x1139F1], base[0x1139F2], base[0x1139F3]);
        emit(m);
    }
    delete h;
    return 0;
}

} // namespace

void lua_bridge_init(const char* profile_id, const char* log_path, ttmod::RuntimeOwner* owner) {
    g_logpath = log_path ? log_path : "";
    if (!ttmod::profile_has(profile_id, ttmod::Capability::LuaBridge)) return;
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) {
        emit("lua: no exe module, skipping");
        return;
    }
    HANDLE t = CreateThread(nullptr, 0, late_hook_thread, new LateHook{exe}, 0, nullptr);
    if (t) CloseHandle(t);
    else emit("lua: late thread failed, skipping");
}

void lua_bridge_shutdown() {
    InterlockedExchange(&g_dead, 1);
    // Drops our observations only. The game still owns every state; nothing
    // here closes or frees one.
    g_states.clear();
}

} // namespace ttmod_win
#endif
