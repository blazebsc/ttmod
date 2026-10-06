// M2 hook stage (Windows-only). Packer-proof design:
//
// - kernel32!CreateFileA: MinHook detour installed at process init (proven
//   safe across many runs; few threads alive that early).
// - kernel32!CreateFileW: IAT hook in the game's import table. The IAT lives
//   in .rdata (never packed), so install is a pointer swap: no disassembly,
//   no trampoline, no thread-freeze. This is the M5 resource-visibility path.
// - ScriptManager::LoadResource detour: REMOVED. MinHook on unpacked game code
//   hangs Wine (thread-freeze racing the unpacker; reproduced 2x). The
//   validated signature + RVA live in docs/research/runtime/load-resource.md
//   for a post-unpack M6 attempt. diagnose_loadresource() below keeps the
//   evidence path (entropy/hex/dumps) without patching anything.
#ifdef _WIN32
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#include "MinHook.h"
#include "events.hpp"
#include "mods.hpp"
#include "ttmod/lua_bridge.hpp"
#include "stage.hpp"
#include "ttmod/events.hpp"
#include "ttmod/log.hpp"
#include "ttmod/lua_bridge.hpp"
#include "ttmod/pathnorm.hpp"
#include "ttmod/runtime.hpp"
#include "ttmod/sigmatch.hpp"
#include "hooks.hpp"
#include "win32_path.hpp"

namespace ttmod_win {
namespace {

// Reference anchors (telltale_hook, reference-only). Used for diagnostics;
// LoadResource is NOT hooked (see header comment).
constexpr DWORD kLoadResourceRVA = ttmod::kLoadResourceAnchor.rva;
constexpr DWORD kLuaNewstateRVA = ttmod::kLuaNewstateAnchor.rva;
constexpr LONG kMaxLoggedHits = 32;
static LONG g_maxw = 0; // per-open trace OFF by default (user-clean logs);
                        // TTMOD_FILELOG_N=N re-enables (diagnostics)

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileAFn g_origCreateFileA = nullptr;
static CreateFileWFn g_origCreateFileW = nullptr;
static std::string g_logpath;
static volatile LONG g_hits = 0;
static volatile LONG g_whits = 0;
static DWORD g_tls = TLS_OUT_OF_INDEXES;

static void emit(const std::string& msg) {
    ttmod::Logger log;
    if (log.open(g_logpath)) log.info(msg);
}

static HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                                      LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE tmpl) {
    if (g_tls != TLS_OUT_OF_INDEXES && !TlsGetValue(g_tls)) {
        TlsSetValue(g_tls, (LPVOID)1);
        LONG n = InterlockedIncrement(&g_hits);
        if (n <= kMaxLoggedHits) {
            char fn[MAX_PATH + 32] = {};
            if (name) strncpy(fn, name, sizeof fn - 1);
            char msg[sizeof fn + 64];
            snprintf(msg, sizeof msg, "[CreateFileA#%ld] %s", (long)n, fn);
            emit(msg);
        }
        TlsSetValue(g_tls, nullptr);
    }
    return g_origCreateFileA(name, access, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                                      LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE tmpl) {
    LPCWSTR use = name;
    wchar_t replaced[MAX_PATH] = {};
    std::string req, repl, winner;
    bool overridden = false;
    bool guarded = g_tls != TLS_OUT_OF_INDEXES && !TlsGetValue(g_tls);
    if (guarded) TlsSetValue(g_tls, (LPVOID)1);
    if (guarded) {
        // M5 resolver: game-root-relative override lookup (index built at init).
        // Only the filename may change; every other argument passes through.
        if (name && mods_try(name, req, repl, winner)) {
            std::string win = to_win(repl);
            if (widen(win, replaced)) {
                use = replaced;
                overridden = true;
            }
        }
        LONG n = InterlockedIncrement(&g_whits);
        if (n <= g_maxw) {
            char msg[1024];
            snprintf(msg, sizeof msg, "[CreateFileW#%ld] %ls", (long)n, use ? use : L"(null)");
            emit(msg);
        }
    }
    HANDLE h = g_origCreateFileW(use, access, share, sa, disp, flags, tmpl);
    if (guarded) {
        // Timeline: first game file open + first override (once each).
        static volatile LONG s_first_open = 0, s_first_ovr = 0;
        if (InterlockedCompareExchange(&s_first_open, 1, 0) == 0)
            stage(g_logpath.c_str(), "first CreateFileW open returned");
        if (overridden && InterlockedCompareExchange(&s_first_ovr, 1, 0) == 0)
            stage(g_logpath.c_str(), "first override served");
        // M7: event fires AFTER the real open returns, so `succeeded` is factual.
        // Order guaranteed: request -> resolve -> override? -> open -> event.
        // Guard is still held: plugin callbacks that open files won't recurse.
        if (!req.empty()) {
            std::string norm = ttmod::normalize_win_path(req);
            ttmod_win::dispatch_file_event(req, norm, overridden ? repl : "", winner,
                                           ttmod::classify_path(norm), overridden,
                                           h != INVALID_HANDLE_VALUE);
        }
        TlsSetValue(g_tls, nullptr);
    }
    return h;
}

// IAT hook: swap the game's import slot for (dll, func). .rdata is never
// packed, so this needs no disassembler and freezes no threads.
static bool iat_hook(const char* dll, const char* func, void* hookfn, void** orig) {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) return false;
    auto* dos = (PIMAGE_DOS_HEADER)exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = (PIMAGE_NT_HEADERS)((BYTE*)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    void* real = (void*)GetProcAddress(GetModuleHandleA(dll), func);
    if (!real) return false;
    auto* imp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)exe + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* mod = (const char*)exe + imp->Name;
        if (_stricmp(mod, dll) != 0) continue;
        if (!imp->OriginalFirstThunk || !imp->FirstThunk) return false;
        auto* oft = (PIMAGE_THUNK_DATA)((BYTE*)exe + imp->OriginalFirstThunk);
        auto* ft = (PIMAGE_THUNK_DATA)((BYTE*)exe + imp->FirstThunk);
        for (; oft->u1.AddressOfData; ++oft, ++ft) {
            if (oft->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            if ((void*)(ULONG_PTR)ft->u1.Function == real) {
                DWORD old = 0;
                if (!VirtualProtect(&ft->u1.Function, sizeof ft->u1.Function, PAGE_READWRITE, &old))
                    return false;
                ft->u1.Function = (DWORD)(ULONG_PTR)hookfn;
                DWORD tmp = 0;
                VirtualProtect(&ft->u1.Function, sizeof ft->u1.Function, old, &tmp);
                *orig = real;
                return true;
            }
        }
        return false; // dll found, func not imported
    }
    return false;
}

static double entropy64(const BYTE* p) {
    long hist[256] = {};
    for (int i = 0; i < 64; ++i) hist[p[i]]++;
    double h = 0;
    for (int i = 0; i < 256; ++i) {
        if (!hist[i]) continue;
        double q = hist[i] / 64.0;
        h -= q * (log(q) / log(2.0));
    }
    return h;
}

// LoadResource evidence (diagnostic only — never patches; see header comment).
static void maybe_dump_text(BYTE* text, DWORD text_size);
static void diagnose_loadresource(HMODULE exe, BYTE* text, DWORD text_size) {
    BYTE* target = (BYTE*)exe + kLoadResourceRVA;
    if (target < text || target + 64 > text + text_size) {
        emit("hooks: LoadResource candidate outside .text");
        return;
    }
    char hmsg[128];
    snprintf(hmsg, sizeof hmsg, "hooks: LoadResource candidate entropy=%.2f at init (packer may not be done)",
             entropy64(target));
    emit(hmsg);
    char hex[160] = {};
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 3, 4, "%02X ", target[i]);
    emit(std::string("hooks: LoadResource candidate bytes: ") + hex);
    BYTE* ns = (BYTE*)exe + kLuaNewstateRVA;
    if (ns >= text && ns + 4 < text + text_size) {
        char m[96];
        snprintf(m, sizeof m, "hooks: lua_newstate anchor bytes: %02X %02X %02X %02X",
                 ns[0], ns[1], ns[2], ns[3]);
        emit(m);
    }
    maybe_dump_text(text, text_size);
}

static void dump_text(BYTE* text, DWORD text_size, const char* path, const char* tag) {
    FILE* f = fopen(path, "wb");
    if (f) {
        size_t w = fwrite(text, 1, text_size, f);
        fclose(f);
        double h = 0;
        {
            long hist[256] = {};
            size_t n = text_size < 65536 ? text_size : 65536;
            for (size_t i = 0; i < n; ++i) hist[text[i]]++;
            for (int i = 0; i < 256; ++i) {
                if (!hist[i]) continue;
                double q = hist[i] / (double)n;
                h -= q * (log(q) / log(2.0));
            }
        }
        char m[256];
        snprintf(m, sizeof m, "hooks: dumped .text %s (%u bytes, head-entropy=%.2f) to %s",
                 tag, (unsigned)w, h, path);
        emit(m);
    } else {
        emit("hooks: .text dump failed (fopen)");
    }
}

struct LateDump {
    BYTE* text;
    DWORD size;
    char path[MAX_PATH];
};

static DWORD WINAPI late_dump_thread(LPVOID p) {
    LateDump* d = (LateDump*)p;
    Sleep(2000);
    dump_text(d->text, d->size, d->path, "late+2s");
    delete d;
    return 0;
}

// Env-gated dumps: immediate + late (+2s) to detect progressive unpacking.
static void maybe_dump_text(BYTE* text, DWORD text_size) {
    char dumppath[MAX_PATH] = {};
    if (GetEnvironmentVariableA("TTMOD_DUMP_TEXT", dumppath, sizeof dumppath) > 0) {
        dump_text(text, text_size, dumppath, "at-init");
        LateDump* d = new LateDump{text, text_size, {}};
        strncpy(d->path, dumppath, sizeof d->path - 1);
        strncat(d->path, ".late", sizeof d->path - strlen(d->path) - 1);
        HANDLE t = CreateThread(nullptr, 0, late_dump_thread, d, 0, nullptr);
        if (t) CloseHandle(t);
        else delete d;
    }
}

} // namespace

void hooks_init(const char* profile_id, const char* log_path) {
    g_logpath = log_path ? log_path : "";
    if (!ttmod::profile_has(profile_id, ttmod::Capability::FileOverrides)) {
        emit(std::string("hooks: profile ") + (profile_id ? profile_id : "?") +
             " not hookable, skipping");
        return;
    }
    g_tls = TlsAlloc();
    char capbuf[32] = {};
    if (GetEnvironmentVariableA("TTMOD_FILELOG_N", capbuf, sizeof capbuf) > 0)
        g_maxw = atol(capbuf);

    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) { emit("hooks: GetModuleHandle(NULL) failed, skipping"); return; }
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { emit("hooks: bad DOS sig, skipping"); return; }
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { emit("hooks: bad NT sig, skipping"); return; }

    BYTE* text = nullptr;
    DWORD text_size = 0;
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            text = (BYTE*)exe + sec->VirtualAddress;
            text_size = sec->Misc.VirtualSize;
            break;
        }
    }
    if (text && text_size) diagnose_loadresource(exe, text, text_size);

    if (MH_Initialize() != MH_OK) { emit("hooks: MinHook init failed, skipping"); return; }
    void* target = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileA");
    if (!target) { emit("hooks: GetProcAddress(CreateFileA) failed, skipping"); return; }
    char tmsg[96];
    snprintf(tmsg, sizeof tmsg, "hooks: CreateFileA resolved @ %p (import-verified, no RVA)", target);
    emit(tmsg);
    if (MH_CreateHook(target, (LPVOID)hook_CreateFileA, (LPVOID*)&g_origCreateFileA) != MH_OK) {
        emit("hooks: MH_CreateHook(CreateFileA) failed, skipping");
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        emit("hooks: MH_EnableHook(CreateFileA) failed, skipping");
        return;
    }
    emit("hooks: installed CreateFileA detour (log-capped passthrough)");
    if (iat_hook("kernel32.dll", "CreateFileW", (void*)hook_CreateFileW,
                 (void**)&g_origCreateFileW)) {
        emit("hooks: installed CreateFileW IAT hook (log-capped passthrough)");
    } else {
        emit("hooks: CreateFileW IAT hook unavailable, continuing without it");
    }
}

} // namespace ttmod_win
#endif
