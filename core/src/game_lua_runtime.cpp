#include "ttmod/game_lua_runtime.hpp"

#include <algorithm>

namespace ttmod {

GameLuaRuntime::GameLuaRuntime(GameLuaRuntimeOptions opt) : opt_(opt) {}

GameLuaRuntime::~GameLuaRuntime() {
    shutdown();
}

int GameLuaRuntime::observe(void* handle) {
    if (!handle) return -1;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        ++capture_order_;
        // We track capture order but the registry doesn't expose it; we can add it later
    }
    return registry_.observe(handle);
}

bool GameLuaRuntime::note_script(void* handle, const char* filename) {
    if (!handle || !filename) return false;
    bool identified = registry_.note_script(handle, filename);
    if (identified) {
        std::lock_guard<std::mutex> lock(mtx_);
        bool has_engine = false, has_menu = false;
        for (auto& e : registry_.entries()) {
            if (e.role == LuaStateRole::Engine) has_engine = true;
            if (e.role == LuaStateRole::Menu) has_menu = true;
        }
        if (has_engine && has_menu && !ready_) {
            ready_ = true;
            ready_cv_.notify_all();
        }
    }
    return identified;
}

bool GameLuaRuntime::observe_state(void* handle, const char* filename) {
    if (!handle) return false;
    observe(handle);
    if (filename) return note_script(handle, filename);
    return false;
}

void GameLuaRuntime::maybe_install_menu_add_wrapper(void* handle) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& e : registry_.entries()) {
        if (e.handle == handle && e.role == LuaStateRole::Menu && !e.menu_add_wrapper_installed) {
            e.menu_add_wrapper_installed = true;
            break;
        }
    }
}

std::vector<LuaStateEntry> GameLuaRuntime::entries() const {
    return registry_.entries();
}

size_t GameLuaRuntime::count() const {
    return registry_.count();
}

bool GameLuaRuntime::contains(void* handle) const {
    return registry_.contains(handle);
}

void* GameLuaRuntime::first(LuaStateRole role) const {
    return registry_.first(role);
}

LuaStateRole GameLuaRuntime::role_of(void* handle) const {
    return registry_.role_of(handle);
}

void GameLuaRuntime::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        ready_ = false;
    }
    registry_.clear();
    allowed_mods_.clear();
}

void GameLuaRuntime::set_allowed_mods(const std::vector<ModId>& allowed) {
    allowed_mods_ = allowed;
}

bool GameLuaRuntime::wait_until_ready() {
    if (opt_.observe_only) return false;
    std::unique_lock<std::mutex> lock(mtx_);
    if (ready_) return true;
    ready_cv_.wait_for(lock, opt_.ready_timeout, [this] { return ready_; });
    return ready_;
}

bool GameLuaRuntime::is_ready() const noexcept {
    std::lock_guard<std::mutex> lock(mtx_);
    return ready_;
}

Result<void> GameLuaRuntime::run_chunk_on_game_thread(void* handle, std::string_view source,
                                                      std::string_view chunk_name) {
    // This MUST be called from the game thread. The Windows hook layer
    // (lua_bridge.cpp) implements the actual luaL_loadbuffer + lua_pcall
    // using the game's Lua ABI. Here we just validate preconditions.
    if (!handle) return Result<void>::fail(Error{"run-chunk", "", errcat::kType, "null handle"});
    if (!contains(handle)) return Result<void>::fail(Error{"run-chunk", "", errcat::kMissing, "unknown state"});
    if (source.empty())
        return Result<void>::fail(Error{"run-chunk", std::string(chunk_name), errcat::kMissing, "empty chunk"});
    if (opt_.observe_only) return Result<void>::fail(Error{"run-chunk", "", errcat::kIO, "observe-only mode"});
    // Actual execution is in lua_bridge.cpp::bridge_run_chunk
    return Result<void>::success();
}

Result<Value> GameLuaRuntime::get_global_on_game_thread(void* handle, std::string_view name) {
    if (!handle) return Result<Value>::fail(Error{"get-global", "", errcat::kType, "null handle"});
    if (!contains(handle)) return Result<Value>::fail(Error{"get-global", "", errcat::kMissing, "unknown state"});
    if (name.empty()) return Result<Value>::fail(Error{"get-global", "", errcat::kType, "empty name"});
    if (opt_.observe_only) return Result<Value>::fail(Error{"get-global", "", errcat::kIO, "observe-only mode"});
    // Actual implementation in lua_bridge.cpp
    return Result<Value>::fail(Error{"get-global", "", errcat::kIO, "not implemented in core"});
}

Result<void> GameLuaRuntime::set_global_on_game_thread(void* handle, std::string_view name, const Value& v) {
    if (!handle) return Result<void>::fail(Error{"set-global", "", errcat::kType, "null handle"});
    if (!contains(handle)) return Result<void>::fail(Error{"set-global", "", errcat::kMissing, "unknown state"});
    if (name.empty()) return Result<void>::fail(Error{"set-global", "", errcat::kType, "empty name"});
    if (opt_.observe_only) return Result<void>::fail(Error{"set-global", "", errcat::kIO, "observe-only mode"});
    return Result<void>::fail(Error{"set-global", "", errcat::kIO, "not implemented in core"});
}

} // namespace ttmod