#include "ttmod/runtime_owner.hpp"

#include "ttmod/script_vm.hpp"
#include "ttmod/script_api.hpp"
#include "ttmod/script_value.hpp"

namespace ttmod {

RuntimeOwner::RuntimeOwner(const char* profile_id, const char* log_path)
    : profile_id_(profile_id ? profile_id : "unknown"),
      log_path_(log_path ? log_path : ""),
      lua_rt_(),
      dispatch_(std::this_thread::get_id()), // game thread ID injected here
      api_(),
      vm_(nullptr),
      safe_mode_(false),
      prepared_(false) {
    // Wire dispatcher to runtime for typed ops
    dispatch_.set_lua_runtime(&lua_rt_);

    // Bind API registry globally for VM trampolines
    bind_api_registry(&api_);
}

RuntimeOwner::~RuntimeOwner() {
    shutdown();
}

bool RuntimeOwner::prepare_runtime(const ModPlan& plan) {
    if (prepared_) return true;
    if (safe_mode_) return false;

    plan_ = plan;

    // Wait for game Lua states to be observed (Engine + Menu)
    if (!lua_rt_.wait_until_ready()) {
        return false; // bridge disabled or timeout
    }

    // Build the binding table for ttmod.* API
    // For now, use Null VM; Lua backend will be created by the Lua backend factory
    // when the Windows layer calls into it.
    // We use the Null VM as a placeholder - the real VM is created by the Windows layer
    // after the bridge is initialized.
    vm_ = make_null_vm(api_);

    prepared_ = true;
    return true;
}

void RuntimeOwner::set_safe_mode(bool on) {
    safe_mode_ = on;
    if (on) {
        lua_rt_.set_allowed_mods({});
        vm_.reset();
    }
}

void RuntimeOwner::shutdown() {
    if (!prepared_ && !safe_mode_ && vm_ == nullptr) return;

    // Stop dispatcher first (releases blocked callers)
    dispatch_.shutdown();

    // Release VM (closes our Lua state; game states untouched)
    vm_.reset();

    // Clear game Lua runtime (observations only; game owns states)
    lua_rt_.shutdown();

    // Clear API registry
    api_.unregister_owner(ApiOwner{ApiOwnerKind::Framework, ""});

    prepared_ = false;
    safe_mode_ = false;
}

} // namespace ttmod