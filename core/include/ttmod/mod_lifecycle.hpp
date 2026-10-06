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

#include "ttmod/result.hpp"

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

} // namespace ttmod