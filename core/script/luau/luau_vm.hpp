#pragma once
// The Luau implementation of ScriptVm (roadmap PR 31; doc §§4, 9, 53).
//
// Same contract, same sandbox shape, same budget mechanism as the Lua
// backend by construction: the two files mirror each other
// function-for-function, and the test suites assert identical behavior
// (isolation, sharing direction, sandbox, errors, callbacks, runner).
// Documented divergences live in third_party/luau/README.ttmod.md and
// as comments at each site below - never silent.
//
// This header is the ONLY place in the project that includes Luau
// headers (lua.h via the vendored VM), and it is not part of the core
// public surface - the same structural rule as lua_vm.hpp (doc §58).
//
// The VM is TTMod-owned. It is never the game's state and never shares one.
#include <cstdint>
#include <map>
#include <string>

#include "ttmod/result.hpp"
#include "ttmod/script_api.hpp"
#include "ttmod/script_value.hpp"
#include "ttmod/script_vm.hpp"

struct lua_State;

namespace ttmod {

struct LuauVmOptions {
    // Names denied at state creation. Luau's base has no load/dofile
    // (nothing to deny there); getfenv/setfenv are the env-escape
    // adjacent names instead (absent from Lua 5.4's base entirely).
    bool sandbox_globals = true;
    // Interrupt budget per chunk/call (safepoint counter, not exact
    // instructions - see reset_budget). 0 disables (not recommended).
    int instruction_budget = 20000000;
    // Memory cap in MB, checked at the same interrupt cadence (an
    // approximate cap, not an allocation limit). 0 disables.
    int memory_budget_mb = 0;
};

// Creates a TTMod-owned Luau VM with the ttmod.* namespace installed from
// `table`. Fails only when the VM cannot be created; a script that fails
// to compile is a per-mod ModRunStatus, never a creation failure.
Result<std::unique_ptr<ScriptVm>> make_luau_vm(ScriptApiRegistry& api, ScriptHost* host,
                                               std::span<const BindingDesc> table,
                                               const LuauVmOptions& opt = LuauVmOptions{});

} // namespace ttmod
