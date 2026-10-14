// TTMod plugin ABI (C ABI, portable). A plugin DLL exports:
//   int ttmod_plugin_init(const ttmod_host* host);
// Return 0 on success. host->log may be called during init.
// v2 appended subscribe/unsubscribe; v3 appended get_state;
// v4 appends get_mod_count/get_mod_info; v5 appends queue_ui_chunk;
// v6 appends register_api (native script bindings, PR 34).
// Older plugins keep working.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define TTMOD_PLUGIN_API_VERSION 6
#define TTMOD_PLUGIN_API_V1 1
#define TTMOD_PLUGIN_API_V2 2
#define TTMOD_PLUGIN_API_V3 3
#define TTMOD_PLUGIN_API_V4 4
#define TTMOD_PLUGIN_API_V5 5
#define TTMOD_PLUGIN_API_V6 6

#ifdef __cplusplus
extern "C" {
#endif

// Event ids (mirror core EventId).
#define TTMOD_EVENT_FILE_OPEN 1
#define TTMOD_EVENT_RESDESC_OPEN 2
#define TTMOD_EVENT_ARCHIVE_OPEN 3

// Precise semantics: file requested + real open attempted (NOT script
// executed). Strings valid during the callback only. Dispatched on the
// caller (game) thread; callbacks must be non-blocking and reentrant.
struct ttmod_event_file {
    int id;                  // one of TTMOD_EVENT_*
    const char* requested;   // original path (UTF-8)
    const char* normalized;  // canonical form
    const char* resolved;    // replacement path or ""
    const char* winner;      // winning mod id or ""
    const char* category;    // "resdesc" | "archive" | "other"
    int overridden;
    int succeeded;           // real open did not fail
    unsigned long thread_id; // caller thread (Win32 LLP64: 32-bit; the ABI
                             // loads plugins on Windows only, so this is stable)
};

typedef void (*ttmod_event_cb)(const struct ttmod_event_file* ev, void* ctx);

// Read-only lifecycle snapshot (M8). "Seen loading" semantics: episodes whose
// archives were observed opening, counters, save dir. Fixed buffers, C-safe.
// Deeper state (scene/characters/vars) is not exposed (Unknown).
#define TTMOD_MAX_EPISODES 8
struct ttmod_state {
    int episodes[TTMOD_MAX_EPISODES]; // episode numbers, first-seen order
    int episode_count;
    int archives_opened;
    int resdesc_opened;
    int saves_observed;
    int others_opened;
    char save_dir[256];
};

// Script values across the C boundary (PR 34, v6): nil/bool/number/string
// cross by value; everything else arrives as TT_UNSUPPORTED (mirrors
// Value's no-Function rule - doc §58). args strings are BORROWED (valid
// during the call only, same rule as event strings); ret strings are
// COPIED by the host during the call, so plugin stack buffers are fine.
// Layout is plain C (int/double/pointer); field order is pinned by
// test_plugin_abi.
typedef enum ttmod_vtype { TT_NIL, TT_BOOL, TT_NUMBER, TT_STRING, TT_UNSUPPORTED } ttmod_vtype;

struct ttmod_value {
    ttmod_vtype type;
    int b;          // TT_BOOL
    double n;       // TT_NUMBER
    const char* s;  // TT_STRING (borrowed in args, copied out of ret)
};

// Plugin callback: 0 = ok (ret holds the value), nonzero = failure (err
// carries the message, err_len bounds it; truncated, never overflowed).
// Must be non-blocking and reentrant (runs on the script thread); must
// not throw across the boundary (a throw is caught host-side and reported
// as a structured error, like every other binding).
typedef int (*ttmod_native_cb)(void* ctx, const struct ttmod_value* args, int nargs, struct ttmod_value* ret,
                               char* err, size_t err_len);

struct ttmod_host {
    int api_version;       // == TTMOD_PLUGIN_API_VERSION
    const char* profile_id; // e.g. "mcsm1_pc_x86"
    const char* game;       // e.g. "minecraft-story-mode"
    int season;             // e.g. 1
    void (*log)(const char* msg); // thread-safe, may be called anytime
    // v2: subscribe returns token (>0) or -1; unsubscribe is best-effort.
    int (*subscribe)(int event_id, ttmod_event_cb cb, void* ctx);
    void (*unsubscribe)(int token);
    // v3: fills caller-owned snapshot, returns 0. Never fails for valid out.
    int (*get_state)(struct ttmod_state* out);
    // v4: mod menu listing (id-sorted, enabled + disabled). Returns count;
    // get_mod_info writes index i, returns 0 (or -1). Strings valid in out.
    int (*get_mod_count)(void);
    int (*get_mod_info)(int index, struct ttmod_modinfo* out);
    // v5: queue a Lua chunk (source text) to execute on the game's script
    // thread. Runs at the NEXT script-resource load, on that Lua state,
    // via the bridge's balanced-stack path; results/errors are invisible
    // to the plugin (no return channel). 0 queued, -1 dropped (queue full
    // or empty input). Chunks may call ttmod_menu_log(...) — registered on
    // every captured state — to trace into ttmod.log. Thread-safe; never
    // blocks on Lua.
    int (*queue_ui_chunk)(const char* code);
    // ABI negotiation (Stage 16): byte size of THIS struct as the host
    // built it. A plugin must check
    //   host->struct_size >= offsetof(ttmod_host, queue_ui_chunk) + sizeof(...)
    // (or simply == sizeof(ttmod_host) for an exact match) before touching
    // fields past its own version. Appended last so every older offset is
    // stable; never insert above this line.
    uint32_t struct_size;
    uint32_t reserved;
    // v6: register a script binding callable from TTMod Lua as
    // ttmod.native["<plugin-id>"].<name> (the registry forces the
    // namespace: a plugin cannot overwrite stable namespaces no matter
    // what ns it passes). Returns a slot token (>0), or negative on
    // error (bad key, duplicate, bad arity). Callable during init or
    // later; duplicate registration fails, it never replaces.
    // Only valid while the host lives (process lifetime); no unregister
    // (matches the no-unload policy - PR 21).
    int (*register_api)(const char* ns, const char* name, ttmod_native_cb cb, void* ctx, int min_args,
                        int max_args);
};

// Menu entry for get_mod_info (M11 overlay menus).
struct ttmod_modinfo {
    char id[128];
    char version[32];
    int enabled;
    int has_plugin;
    int packaged;
};

#ifdef __cplusplus
}
#endif
