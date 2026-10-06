#pragma once
// The Lua implementation of ScriptVm (doc §§4, 9, 53).
//
// This header is the ONLY place in the project that includes lua.h, and it is
// not part of the core public surface. That is what makes doc §58 structural:
// core includes script_vm.hpp, which cannot name a lua_State even by
// accident, because the type does not exist there.
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

struct LuaVmOptions {
    // Names denied at state creation. A mod script has no business reaching
    // these inside a game process, and removing them costs nothing: TTMod's
    // own module loader (doc §4.1) does not use require/package.
    bool sandbox_globals = true;
    // Instruction budget per chunk/call. 0 disables (not recommended).
    int instruction_budget = 20000000;
};

// Creates a TTMod-owned Lua 5.4 VM with the ttmod.* namespace installed from
// `table`. Fails only when the VM cannot be created; a script that fails to
// compile is a per-mod ModRunStatus, never a creation failure.
Result<std::unique_ptr<ScriptVm>> make_lua_vm(ScriptApiRegistry& api, ScriptHost* host,
                                              std::span<const BindingDesc> table,
                                              const LuaVmOptions& opt = LuaVmOptions{});

} // namespace ttmod