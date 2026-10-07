// GameLuaRuntime: authoritative game Lua state lifecycle (doc §§27, 30, 57).
//
// The game creates and destroys every Lua state; TTMod observes, never owns.
// This is the portable, unit-testable layer; Windows-specific hook logic stays
// in loader/windows/lua/lua_bridge.cpp and calls these methods.
#include <mutex>
#include <string>
#include <vector>

#include "ttmod/game_lua.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/result.hpp"
#include "ttmod/script_value.hpp"

namespace ttmod {

// Options for GameLuaRuntime construction.
struct GameLuaRuntimeOptions {
    // When true, the runtime operates in observe-only mode (no hooks,
    // no chunk execution). Used by safe mode and the test double.
    bool observe_only = false;
};

// The runtime owns the registry and coordinates with ModPlan lifecycle.
class GameLuaRuntime {
  public:
    explicit GameLuaRuntime(GameLuaRuntimeOptions opt = GameLuaRuntimeOptions{});
    ~GameLuaRuntime();

    GameLuaRuntime(const GameLuaRuntime&) = delete;
    GameLuaRuntime& operator=(const GameLuaRuntime&) = delete;

    // Called from LoadResource hook when a state is created.
    // Returns the capture order (1-based) or -1 on failure.
    int observe(void* handle);

    // Called from LoadResource hook when a script finishes loading.
    // Returns true when this script identified the state's role.
    bool note_script(void* handle, const char* filename);

    // GameLuaRegistry delegation (thin facade; registry stays the source of truth).
    [[nodiscard]] std::vector<LuaStateEntry> entries() const;
    [[nodiscard]] size_t count() const;
    [[nodiscard]] bool contains(void* handle) const;
    [[nodiscard]] void* first(LuaStateRole role) const;
    [[nodiscard]] LuaStateRole role_of(void* handle) const;

    // Shutdown: clears internal state. Game still owns every state.
    void shutdown();

    // ModPlan coordination: mark which mods are allowed to use game Lua.
    // Called once per ModPlan rebuild.
    void set_allowed_mods(const std::vector<ModId>& allowed);

    // Game-thread operations (must be called FROM the game thread).
    // These are the real implementations behind GameDispatcher's portable interface.
    Result<void> run_chunk_on_game_thread(void* handle, std::string_view source, std::string_view chunk_name);
    Result<Value> get_global_on_game_thread(void* handle, std::string_view name);
    Result<void> set_global_on_game_thread(void* handle, std::string_view name, const Value& v);

    [[nodiscard]] bool observe_only() const noexcept {
        return opt_.observe_only;
    }

  private:
    GameLuaRuntimeOptions opt_;
    GameLuaRegistry registry_;
    std::vector<ModId> allowed_mods_;
};

} // namespace ttmod