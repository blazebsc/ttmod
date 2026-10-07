#pragma once
// RuntimeOwner: single coordination object for the TTMod runtime (doc §70).
//
// Owns GameLuaRuntime, GameDispatcher, ScriptApiRegistry, and ScriptVm.
// Created once in InitThread, passed to Windows loader components.
// Eliminates the global statics (g_states, g_loadstring, etc.) that
// currently couple lua_bridge.cpp, menu_bridge.cpp, plugins.cpp.
#include <memory>
#include <string>
#include <vector>

#include "ttmod/game_lua_runtime.hpp"
#include "ttmod/game_dispatch.hpp"
#include "ttmod/script_api.hpp"
#include "ttmod/script_vm.hpp"
#include "ttmod/modplan.hpp"
#include "ttmod/modid.hpp"

namespace ttmod {

class RuntimeOwner : public ScriptHost {
  public:
    // Constructs with profile + log path. Does NOT start observing states yet.
    explicit RuntimeOwner(const char* profile_id, const char* log_path);
    ~RuntimeOwner();

    RuntimeOwner(const RuntimeOwner&) = delete;
    RuntimeOwner& operator=(const RuntimeOwner&) = delete;

    // Call after ModPlan is built. Observes states, waits for ready.
    // Returns false if Lua bridge disabled, no states seen, or timeout.
    bool prepare_runtime(const ModPlan& plan);

    // Safe mode: blocks third-party mods, but framework runtime stays active.
    void set_safe_mode(bool on);
    [[nodiscard]] bool safe_mode() const noexcept {
        return safe_mode_;
    }

    // Accessors for native plugins (host ABI).
    GameDispatcher& dispatcher() noexcept override {
        return dispatch_;
    }
    GameLuaRuntime& runtime() noexcept override {
        return lua_rt_;
    }
    ScriptApiRegistry& api_registry() noexcept override {
        return api_;
    }

    // Get or create the shared ScriptVm. Returns null if safe_mode or no runtime.
    // Attempts to upgrade from Null VM to real Lua VM if states are ready.
    ScriptVm* vm() noexcept;

    // Set the ScriptVm (called by Windows layer when real VM is ready).
    void set_vm(std::unique_ptr<ScriptVm> vm);

    // Ensure real Lua VM is created if states are ready and we're still on Null VM.
    // Returns true if real VM is now active. Override in platform layer for real VM creation.
    virtual bool ensure_real_vm();

    // Pump dispatcher (call from game thread, e.g. LoadResource hook).
    void pump_dispatcher() {
        dispatch_.pump();
    }

    // Shutdown sequence: stop dispatcher, clear runtime, release VM.
    void shutdown();

  private:
    const char* profile_id_;
    const char* log_path_;
    GameLuaRuntime lua_rt_;
    GameDispatcher dispatch_;
    ScriptApiRegistry api_;
    std::unique_ptr<ScriptVm> vm_;
    ModPlan plan_;
    bool safe_mode_ = false;
    bool prepared_ = false;
};

} // namespace ttmod