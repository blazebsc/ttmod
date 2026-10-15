#pragma once
// Mod run lifecycle (doc §36).
//
// The first half of §36 (Discovered -> ... -> Dependency-resolved) is ALREADY
// produced by ModPlan, which is structured and already drives the CLI and the
// menu. This header does not re-model it. It starts where the plan stops.
//
// A failed stage produces a structured Failure, never a vague bool (§36).
#include <cstdint>
#include <string>
#include <vector>

#include "ttmod/manifest.hpp"
#include "ttmod/result.hpp"
#include "ttmod/script_vm.hpp"

namespace ttmod {

enum class ModRunState {
    Prepared, // in the plan, script runtime declared, entrypoint not yet run
    Running,  // entrypoint executed successfully
    Stopping,
    Unloaded,
    Failed, // see Failure
};
const char* to_string(ModRunState s);

// Which step failed. Enough granularity to act on, no more.
enum class ModStage {
    Entrypoint, // declared entrypoint missing or unreadable
    Backend,    // no backend for a declared runtime
    Compile,    // syntax error in the chunk
    Init,       // error while running the entrypoint
    Teardown,
};
const char* to_string(ModStage s);

// Reuses Error verbatim: no second error vocabulary in this codebase.
struct Failure {
    ModStage stage = ModStage::Init;
    Error error; // object = "chunk:line", message = "msg\ntraceback"
    [[nodiscard]] std::string describe() const;
};

struct ModRunStatus {
    std::string id; // serialized id; ModId is used everywhere else
    ModRunState state = ModRunState::Prepared;
    Failure failure;
    uint32_t errors = 0;
    // One line for the menu, e.g. "main.lua:12: attempt to index a nil value".
    [[nodiscard]] std::string last_error_line() const;
};

// Pure transition table. Stringly-typed on the EVENT side only ("load",
// "ok", "fail", "stop", "unloaded"), matching mode_for_status()'s existing
// shape in runtime.hpp: a variant state machine would be more type-safe and
// more code than the thing it models.
//
// Rules that matter:
// - Failed is terminal. A failed mod never "recovers" into Running; it must
//   go through Unloaded first (§63: a broken mod is disabled, not retried in
//   a loop).
// - Unloaded is terminal too, except that Prepared/Running may stop.
ModRunState advance(ModRunState from, const char* event);

// Entrypoint selection (PR 32/33): which enabled mods contribute a script
// file for a given runtime. In ModPlan load order; keeps mods declaring
// that runtime with a non-empty entrypoint path. Pure selection - reading
// the file (mod dirs) and running/queueing it is loader-side.
struct ScriptModEntry {
    std::string id;
    Runtime runtime = Runtime::Lua;
    std::string entrypoint; // manifest-relative path, may be empty
    bool enabled = true;
};
struct SelectedEntrypoint {
    std::string id;
    std::string entrypoint; // manifest-relative path, passed through
};
std::vector<SelectedEntrypoint> collect_script_entrypoints(const std::vector<ScriptModEntry>& mods, Runtime runtime);
// Telltale entrypoint selection (PR 32): which enabled mods contribute a
// game.lua to the game's own Lua states. In ModPlan load order; keeps mods
// whose manifest declares the telltale-lua runtime with a non-empty
// entrypoint path. Pure selection - reading the file (mod dirs) and
// queueing it onto the game thread is loader-side.
struct TelltaleMod {
    std::string id;
    bool telltale_runtime = false;
    std::string entrypoint; // manifest-relative game.lua, may be empty
    bool enabled = true;
};
struct TelltaleEntrypoint {
    std::string id;
    std::string entrypoint; // manifest-relative path, passed through
};
std::vector<TelltaleEntrypoint> collect_telltale_entrypoints(const std::vector<TelltaleMod>& mods);

// One script entrypoint to run (PR 30): a mod id, its declared runtime, and
// the already-loaded source. Loading files is caller-side (only the loader
// knows mod dirs); this runner owns environments, execution, and status.
struct ModScript {
    std::string id;
    Runtime runtime = Runtime::Lua;
    std::string chunk_name;
    std::string source;
    // Declared permissions (manifest runtime.permissions, converted by the
    // caller). Bindings gated by caller_permitted() fail for a mod that did
    // not declare the permission - per-env capability filtering.
    std::vector<Permission> permissions;
};

// Caller capability context (PR 17 follow-up): while run_mod_entrypoints
// executes a mod's chunk, the mod's DECLARED permissions are installed
// here. Bindings call caller_permitted(p) to gate themselves; outside an
// entrypoint run (framework code, plugin callbacks, plain run_chunk) there
// is no context and everything is permitted - framework trust.
void set_caller_permissions(const std::vector<Permission>* perms);
[[nodiscard]] bool caller_permitted(Permission p);

// Run every entrypoint on one shared VM, one environment per mod (PR 17).
// Per mod, in order: wrong runtime -> Backend failure; empty source ->
// Entrypoint failure (declared but unreadable); env creation failure ->
// Backend failure; run failure -> Init failure (the Lua backend reports
// compile and runtime errors identically, so Compile is reserved, not
// produced); success -> Running. Statuses come back in input order with
// errors counted and last_error_line() ready for the menu. No logging
// here (no sink in core) - the caller reports statuses as diag Script
// events. Never throws, never touches the game.
std::vector<ModRunStatus> run_mod_entrypoints(ScriptVm& vm, const std::vector<ModScript>& mods);

} // namespace ttmod