#pragma once
// Game-thread dispatcher (doc §§12, 42, 71).
//
// TTMod script threads must never call game Lua or game-native state
// directly. Everything crosses as an Op carrying marshaled Values, and Ops
// run on the game thread or not at all. This is the single controlled point
// for thread validation, state validation, scheduling, synchronization and
// result marshaling.
//
// The game-thread identity is INJECTED, not discovered: on Windows it is the
// id captured when the proxy DLL is loaded (the loading thread IS the game's
// thread); in tests it is the test's thread. No Windows code here, so this is
// fully testable off-target.
//
// The dispatcher has no idea a scripting language exists. That is deliberate:
// it is a typed generalization of uiqueue, not a script facility.
//
// Typed operations (run_on_state, create_mod_env, queue_plugin_chunk,
// install_c_function) resolve role->handle at PUMP TIME, not enqueue time.
// This is critical: the game creates/destroys states asynchronously, so a
// handle captured at enqueue time may be dead by pump time.
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

// Forward declaration to break circular dependency: GameLuaRuntime includes
// GameDispatcher for options, GameDispatcher needs GameLuaRuntime* for typed ops.
class GameLuaRuntime;

#include "ttmod/game_lua_runtime.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/result.hpp"
#include "ttmod/script_value.hpp"

namespace ttmod {

struct GameDispatchOptions {
    // A call that cannot be serviced in this long returns a structured
    // timeout. Never a hang, never a game freeze: the script is on the
    // other side of this and can do nothing until it returns.
    std::chrono::milliseconds timeout{2000};
    // Fixed, like uiqueue: a flood drops rather than growing without bound.
    // Drops are counted, never silent.
    size_t capacity = 256;
};

class GameDispatcher {
  public:
    using Op = std::function<Result<Value>()>;
    using Options = GameDispatchOptions;

    explicit GameDispatcher(std::thread::id game_thread, Options opt = GameDispatchOptions{});
    ~GameDispatcher();

    GameDispatcher(const GameDispatcher&) = delete;
    GameDispatcher& operator=(const GameDispatcher&) = delete;

    // Inline when already on the game thread (no queueing, no locking);
    // otherwise enqueue and block up to Options::timeout.
    Result<Value> call(Op op);
    // Fire-and-forget. Dropped and counted when the queue is full.
    void post(Op op);

    // Runs queued ops in arrival order. CALL ONLY from the game thread.
    // Returns how many ran. Ops enqueued during a drain are left for the next
    // drain, so a drain always terminates.
    size_t pump();

    // Completes every pending call with a structured error. Idempotent.
    void shutdown();

    [[nodiscard]] size_t dropped() const;
    [[nodiscard]] size_t pending() const;
    [[nodiscard]] bool on_game_thread() const;

    // ===== Typed operations for common game-thread tasks =====
    //
    // These resolve role -> handle AT PUMP TIME (inside the game thread).
    // This is critical: the game creates/destroys states asynchronously,
    // so a handle captured at enqueue time may be dead by pump time.

    // Run a Lua chunk on the state matching `role`.
    // The op captures the state handle at pump time, not at enqueue time.
    Result<Value> run_on_state(LuaStateRole role,
                               std::string_view chunk,
                               const Value& env = Value::nil());

    // Get a snapshot of the Lua state registry.
    Result<std::vector<LuaStateEntry>> get_state_snapshot();

    // Queue a plugin chunk (same as uiqueue_push but via dispatcher).
    bool queue_plugin_chunk(std::string_view code);

    // Create isolated _ENV for a mod on the game's Menu state.
    // Returns the environment Value (or error).
    Result<Value> create_mod_env(const ModId& mod_id);

    // Install a C function on a specific game state (for framework functions).
    Result<void> install_c_function(LuaStateRole role,
                                    const char* name,
                                    void* c_fn); // lua_CFunction

    // Not owned; set by RuntimeOwner after both are constructed.
    void set_lua_runtime(GameLuaRuntime* rt) { lua_runtime_ = rt; }

  private:
    // Per-waiting-caller state. Heap-allocated and shared_ptr-owned so a
    // caller that times out cannot leave the pump writing into freed memory.
    struct Waiter {
        std::mutex m;
        std::condition_variable cv;
        Result<Value> result = Result<Value>::ok(Value::nil());
        bool done = false;
    };
    // A queued unit of work: the op, plus the waiter to answer (null for a
    // fire-and-forget post).
    struct Task {
        Op op;
        std::shared_ptr<Waiter> waiter;
    };

    const std::thread::id game_thread_;
    Options opt_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<Task> queue_;
    size_t dropped_ = 0;
    bool stopped_ = false;

    // Not owned; set by RuntimeOwner after both are constructed.
    GameLuaRuntime* lua_runtime_ = nullptr;
};

} // namespace ttmod