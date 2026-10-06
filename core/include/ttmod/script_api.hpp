#pragma once
// Versioned script API registry (doc §§13, 34, 53, 75).
//
// This is the answer to "how does a function get into the VM, and how do we
// avoid dangling callbacks when its owner goes away".
//
// The VM never stores a plugin's function pointer. It installs ONE generic
// trampoline per slot, whose only captured datum is the slot index. The
// plugin's NativeFn lives in an ApiEntry inside host-owned storage. Revoking
// turns the entry into a tombstone: the installed stub keeps its slot and
// every later call returns a structured "missing" error. A dangling call is
// therefore not a failure mode this design has to defend against at runtime -
// it has nowhere to come from.
//
// TTMod's own ttmod.* bindings are registered through the SAME registry, so
// there is one dispatch path rather than one for the framework and one for
// plugins (which is what keeps Lua and a future Luau semantically identical,
// doc §53).
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "ttmod/result.hpp"
#include "ttmod/script_value.hpp"
#include "ttmod/version.hpp" // kApiVersion

namespace ttmod {

// "ttmod.events.on" -> {ns:"events", name:"on"}. Plugin registrations are
// re-homed under "native/<mod-id>" by the registry, so a plugin cannot squat a
// stable namespace such as "game".
struct ApiKey {
    std::string ns;
    std::string name;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string dotted() const;
    bool operator==(const ApiKey& o) const {
        return ns == o.ns && name == o.name;
    }
    bool operator<(const ApiKey& o) const {
        if (ns != o.ns) return ns < o.ns;
        return name < o.name;
    }
};

Result<ApiKey> parse_api_key(std::string_view dotted);

enum class ApiOwnerKind { Framework, Mod, Plugin };

struct ApiOwner {
    ApiOwnerKind kind = ApiOwnerKind::Framework;
    std::string id; // "" for Framework; mod id or plugin id otherwise

    [[nodiscard]] bool operator==(const ApiOwner& o) const {
        return kind == o.kind && id == o.id;
    }
    // Plugin-owned keys are namespaced as "native/<id>", so a plugin cannot
    // overwrite a stable namespace no matter what it passes in.
    [[nodiscard]] std::string effective_ns() const {
        if (kind == ApiOwnerKind::Plugin && !id.empty()) return "native/" + id;
        if (kind == ApiOwnerKind::Mod && !id.empty()) return "native/" + id;
        return "";
    }
};

// Everything a binding may touch. Deliberately small: a binding's only powers
// are "read my arguments" and "reach the host".
struct CallCtx {
    const Value* args = nullptr;
    size_t nargs = 0;
    uint32_t api_version = 0; // kApiVersion at call time (plugin_api.h convention)
    const ApiOwner* caller = nullptr;
    class ScriptHost* host = nullptr;

    [[nodiscard]] const Value* arg(size_t i) const noexcept;
    [[nodiscard]] std::string arg_str(size_t i) const;
    [[nodiscard]] double arg_num(size_t i, double def = 0) const;
};

using NativeFn = std::function<Result<Value>(CallCtx&)>;

struct ApiEntry {
    ApiKey key;
    ApiOwner owner;
    uint32_t since_api = 0;
    int min_args = 0;
    int max_args = -1; // -1 = variadic
    NativeFn fn;
    bool revoked = false; // tombstone: the slot stays, calls now fail
};

class ScriptApiRegistry {
  public:
    // Rejects: duplicate key (kDuplicate), since_api > kApiVersion (kRange),
    // missing/invalid key, empty fn. Returns the revocation token.
    Result<uint32_t> register_fn(const ApiOwner& owner, ApiKey key, NativeFn fn, uint32_t since_api, int min_args,
                                 int max_args);

    // Tombstone, never erase: an installed stub keeps its slot and turns every
    // later call into Error{category:"missing"}.
    void unregister(uint32_t token);
    void unregister_owner(const ApiOwner& owner);

    [[nodiscard]] const ApiEntry* find(const ApiKey& k) const;
    [[nodiscard]] std::vector<ApiKey> keys() const; // sorted, deterministic

    // Called by the VM trampoline. Arity-checked and never throws: a plugin
    // NativeFn that unwinds is a plugin bug and must not cross the boundary,
    // so it is caught here and reported as a structured error.
    Result<Value> invoke(uint32_t token, const Value* args, size_t nargs, const ApiOwner* caller) const;

    [[nodiscard]] size_t size() const;

  private:
    mutable std::mutex mtx_;
    std::map<uint32_t, ApiEntry> entries_; // token -> entry
    std::map<ApiKey, uint32_t> by_key_;
    uint32_t next_token_ = 1;
};

// Process-wide registry the VM trampoline dispatches through. The VM holds no
// function pointers, only token numbers, so it needs a way back here; one
// registry exists per process because the game loads exactly one framework.
ScriptApiRegistry& bound_api_registry();
void bind_api_registry(ScriptApiRegistry* r);

} // namespace ttmod