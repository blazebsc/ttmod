#pragma once
// Game Lua state registry (Step 8, ADR-006).
//
// The game creates and destroys every Lua state; TTMod only observes them.
// This is the portable half of that observation: identity, role, lifecycle
// and the script-based role inference, with no Lua headers and no Windows
// dependency, so it can be unit-tested off-target.
//
// What it deliberately does NOT do:
// - Own any state. Never call lua_close on anything seen here.
// - Hand raw state pointers to scripts (that is the native layer's job,
//   and it uses opaque handles).
// - Assume there is one global state. MCSM1 has at least two (engine +
//   menu) and the count is not fixed by anything we control.
//
// Ownership: the registry is owned by the native runtime, which outlives
// every state it observes. States may be destroyed underneath us; the
// registry treats a state pointer as an opaque identity token and never
// dereferences it.
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace ttmod {

// Role of an observed state. Inferred from the scripts the state loads,
// never guessed from the capture order.
enum class LuaStateRole {
    Unknown, // captured, no identifying script seen yet
    Engine,  // loaded an engine script (_engine.lua, ...)
    Menu,    // loaded Menu.lua
    Other,   // third state family, not yet characterised per profile
};

inline const char* to_string(LuaStateRole r) {
    switch (r) {
    case LuaStateRole::Engine:
        return "engine";
    case LuaStateRole::Menu:
        return "menu";
    case LuaStateRole::Other:
        return "other";
    default:
        return "unknown";
    }
}

struct LuaStateEntry {
    void* handle = nullptr; // opaque: a lua_State* in the native layer
    int order = 0;          // capture sequence, 1-based (diagnostics only)
    LuaStateRole role = LuaStateRole::Unknown;
    int scripts_seen = 0;
    std::string first_script; // what identified the role ("" if none)
};

// Thread-safe registry. All methods are safe to call from any thread: the
// LoadResource hook runs on game threads, and state creation can race with
// shutdown.
class GameLuaRegistry {
  public:
    // Called from the lua_newstate hook. Returns the capture order.
    int observe(void* handle);
    // Called from the LoadResource hook. Returns true when this script is
    // what identified the state (so the caller can log it once).
    bool note_script(void* handle, const char* filename);

    [[nodiscard]] std::vector<LuaStateEntry> entries() const;
    [[nodiscard]] size_t count() const;
    [[nodiscard]] bool contains(void* handle) const;
    // First state with the given role, or nullptr. "engine"/"menu" are the
    // roles the runtime needs to address; Other/Unknown never match.
    [[nodiscard]] void* first(LuaStateRole role) const;
    [[nodiscard]] LuaStateRole role_of(void* handle) const;

    void clear(); // shutdown; the game still owns every state

    // Role inference, exposed for testing and per-profile override.
    // Returns Unknown when the script identifies nothing.
    static LuaStateRole role_for_script(const char* filename);

  private:
    mutable std::mutex mtx_;
    std::vector<LuaStateEntry> states_;
};

} // namespace ttmod