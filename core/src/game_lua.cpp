#include "ttmod/game_lua.hpp"

#include <cstring>

namespace ttmod {
namespace {

bool tail_matches(const char* path, const char* tail) {
    if (!path || !tail) return false;
    size_t n = strlen(path), m = strlen(tail);
    return n >= m && strcmp(path + n - m, tail) == 0;
}

} // namespace

LuaStateRole GameLuaRegistry::role_for_script(const char* filename) {
    if (!filename || !*filename) return LuaStateRole::Unknown;
    // Menu is checked first: on MCSM1 the menu state also loads engine-ish
    // shared scripts, and "menu" is the more specific answer.
    if (tail_matches(filename, "Menu.lua")) return LuaStateRole::Menu;
    static const char* kEngineScripts[] = {"_engine.lua", "EngineTypes.lua", "StoryBoardTracker.lua"};
    for (auto* t : kEngineScripts)
        if (tail_matches(filename, t)) return LuaStateRole::Engine;
    return LuaStateRole::Unknown;
}

int GameLuaRegistry::observe(void* handle) {
    if (!handle) return 0;
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& s : states_)
        if (s.handle == handle) return s.order; // already captured
    LuaStateEntry e;
    e.handle = handle;
    e.order = (int)states_.size() + 1;
    states_.push_back(e);
    return e.order;
}

bool GameLuaRegistry::note_script(void* handle, const char* filename) {
    if (!handle || !filename || !*filename) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& s : states_) {
        if (s.handle != handle) continue;
        ++s.scripts_seen;
        // Already identified: role never changes once known (a menu state
        // loading an engine script must not be relabelled).
        if (s.role != LuaStateRole::Unknown) return false;
        LuaStateRole r = role_for_script(filename);
        if (r == LuaStateRole::Unknown) return false;
        s.role = r;
        s.first_script = filename;
        return true;
    }
    return false;
}

std::vector<LuaStateEntry> GameLuaRegistry::entries() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return states_;
}

size_t GameLuaRegistry::count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return states_.size();
}

bool GameLuaRegistry::contains(void* handle) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& s : states_)
        if (s.handle == handle) return true;
    return false;
}

void* GameLuaRegistry::first(LuaStateRole role) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& s : states_)
        if (s.role == role) return s.handle;
    return nullptr;
}

LuaStateRole GameLuaRegistry::role_of(void* handle) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& s : states_)
        if (s.handle == handle) return s.role;
    return LuaStateRole::Unknown;
}

void GameLuaRegistry::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    states_.clear();
}

} // namespace ttmod