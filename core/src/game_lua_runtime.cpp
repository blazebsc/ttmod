#include "ttmod/game_lua_runtime.hpp"

#include <algorithm>

namespace ttmod {

GameLuaRuntime::GameLuaRuntime(GameLuaRuntimeOptions opt) : opt_(opt) {}

GameLuaRuntime::~GameLuaRuntime() {
    shutdown();
}

int GameLuaRuntime::observe(void* handle) {
    if (!handle) return -1;
    return registry_.observe(handle);
}

bool GameLuaRuntime::note_script(void* handle, const char* filename) {
    if (!handle || !filename) return false;
    return registry_.note_script(handle, filename);
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
    registry_.clear();
    allowed_mods_.clear();
}

void GameLuaRuntime::set_allowed_mods(const std::vector<ModId>& allowed) {
    allowed_mods_ = allowed;
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