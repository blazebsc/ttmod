#pragma once
// The backend-agnostic script VM contract (doc §§4, 9, 53, 55).
//
// Six operations, all Result-returning (never exceptions), all called ONLY
// from the script thread. Lua implements this; Luau will implement the same
// thing later; the binding layer above it is written once so both languages
// expose identical semantics (doc §53).
//
// Deliberately NOT here, and enforced by the type system rather than by
// convention:
//   - no lua_State, no Luau global, no raw interpreter handle of any kind.
//     A raw handle could not be named in this header without including that
//     VM's header, which is what makes doc §58 structural (core does not
//     link or see a VM).
//   - no raw game pointer. Game objects cross as GameObjectHandle only.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "ttmod/result.hpp"
#include "ttmod/script_api.hpp"
#include "ttmod/script_value.hpp"

namespace ttmod {

enum class ScriptBackendKind {
    Lua,
    // No VM: the script layer is inert. Used when safe mode is on, when no
    // mod declares a script runtime, and as the test double - one
    // implementation, two jobs, no separate fake to keep correct.
    Null,
};
const char* to_string(ScriptBackendKind k);

// Host-side reference to a function that lives inside a VM. Opaque to scripts
// in the sense that matters: it is not a VM handle, it means nothing in any
// other VM, and it is invalid after release().
struct CallbackRef {
    uint32_t slot = 0; // 0 = none
    [[nodiscard]] bool valid() const noexcept {
        return slot != 0;
    }
};

class ScriptVm {
  public:
    virtual ~ScriptVm() = default;

    [[nodiscard]] virtual const char* backend() const noexcept = 0;
    [[nodiscard]] virtual size_t bytes_used() const noexcept = 0;

    // Compile + run one chunk. `env` selects the function environment; the
    // shared-VM decision passes the single globals table, so `env` is always
    // nil in practice and exists for the future per-mod environment.
    // Error object = "chunk:line", message = "msg\ntraceback".
    virtual Result<Value> run_chunk(std::string_view source, std::string_view chunk_name, const Value& env) = 0;

    // Turn a function Value (produced by CallCtx::take_callback) into a
    // host-side ref, call it, and release it.
    virtual Result<CallbackRef> retain(const Value& fn) = 0;
    virtual void release(CallbackRef ref) = 0;
    virtual Result<Value> call(CallbackRef ref, const Value* args, size_t nargs) = 0;

    // Install the generic trampoline for one registry slot. The VM stores only
    // the slot index, never a plugin's function pointer, so unloading the
    // owning plugin cannot leave a dangling call in the VM.
    virtual Result<void> install_api(uint32_t slot, const ApiKey& key, uint32_t api_version) = 0;

  protected:
    ScriptVm() = default;
};

// Built here (core never links Lua); the Lua backend lives in script/lua/.
std::unique_ptr<ScriptVm> make_null_vm(ScriptApiRegistry& api);

// One row of the ttmod.* namespace table. Installing from a single list is what
// makes Lua and a future Luau expose the SAME API instead of two APIs that
// drift (doc §53).
struct BindingDesc {
    const char* ns;   // "log", "events", "mods", "config", "game"
    const char* name; // "info", "on", "get", "list"
    NativeFn fn;
    int min_args = 0;
    int max_args = -1; // -1 = variadic
};

// Registers every row and installs the corresponding trampolines.
Result<void> install_bindings(ScriptVm& vm, ScriptApiRegistry& api, ScriptHost* host,
                              std::span<const BindingDesc> table);

} // namespace ttmod