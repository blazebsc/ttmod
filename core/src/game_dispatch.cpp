#include "ttmod/game_dispatch.hpp"

#include <memory>

namespace ttmod {

GameDispatcher::GameDispatcher(std::thread::id game_thread, Options opt)
    : game_thread_(game_thread), opt_(opt) {}

GameDispatcher::~GameDispatcher() {
    shutdown();
}

bool GameDispatcher::on_game_thread() const {
    return std::this_thread::get_id() == game_thread_;
}

// Internal queue logic for blocking call returning Result<Value>.
Result<Value> GameDispatcher::call(Op op) {
    if (!op) return Result<Value>::fail(Error{"dispatch", "", errcat::kType, "null op"});
    if (on_game_thread()) return op();

    auto w = std::make_shared<Waiter>();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) return Result<Value>::fail(Error{"dispatch", "", errcat::kIO, "dispatcher shut down"});
        if (queue_.size() >= opt_.capacity) {
            ++dropped_;
            return Result<Value>::fail(Error{"dispatch", "", errcat::kLimit, "game queue full"});
        }
        queue_.push(Task{std::move(op), w});
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(w->m);
    if (!w->cv.wait_for(lock, opt_.timeout, [&] { return w->done; })) {
        return Result<Value>::fail(Error{"dispatch", "", errcat::kIO, "game dispatch timed out"});
    }
    return std::move(w->result);
}

void GameDispatcher::post(Op op) {
    if (!op) return;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) return;
        if (queue_.size() >= opt_.capacity) {
            ++dropped_;
            return;
        }
        queue_.push(Task{std::move(op), nullptr});
    }
    cv_.notify_all();
}

size_t GameDispatcher::pump() {
    std::queue<Task> batch;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        batch.swap(queue_);
    }
    size_t ran = 0;
    while (!batch.empty()) {
        Task t = std::move(batch.front());
        batch.pop();
        Result<Value> r = t.op();
        ++ran;
        if (t.waiter) {
            {
                std::lock_guard<std::mutex> lock(t.waiter->m);
                t.waiter->result = std::move(r);
                t.waiter->done = true;
            }
            t.waiter->cv.notify_all();
        }
    }
    return ran;
}

void GameDispatcher::shutdown() {
    std::queue<Task> leftovers;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) return;
        stopped_ = true;
        leftovers.swap(queue_);
    }
    while (!leftovers.empty()) {
        Task t = std::move(leftovers.front());
        leftovers.pop();
        if (!t.waiter) continue;
        {
            std::lock_guard<std::mutex> lock(t.waiter->m);
            t.waiter->result = Result<Value>::fail(Error{"dispatch", "", errcat::kIO, "dispatcher shut down"});
            t.waiter->done = true;
        }
        t.waiter->cv.notify_all();
    }
    cv_.notify_all();
}

size_t GameDispatcher::dropped() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return dropped_;
}

size_t GameDispatcher::pending() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return queue_.size();
}

// ===== Typed operations =====

Result<Value> GameDispatcher::run_on_state(LuaStateRole role,
                                           std::string_view chunk,
                                           const Value& env) {
    return call([this, role, chunk, &env]() -> Result<Value> {
        if (!lua_runtime_) return Result<Value>::fail(Error{"run-on-state", "", errcat::kIO, "no lua runtime set"});
        auto handle = lua_runtime_->first(role);
        if (!handle) return Result<Value>::fail(Error{"run-on-state", "", errcat::kMissing, "no state for role"});
        auto r = lua_runtime_->run_chunk_on_game_thread(handle, chunk, "");
        if (!r.ok()) return Result<Value>::fail(r.error());
        return Result<Value>::ok(Value::nil());
    });
}

Result<std::vector<LuaStateEntry>> GameDispatcher::get_state_snapshot() {
    // Synchronous on game thread; off-thread returns error.
    if (on_game_thread()) {
        if (!lua_runtime_) return Result<std::vector<LuaStateEntry>>::fail(Error{"get-snapshot", "", errcat::kIO, "no lua runtime set"});
        return Result<std::vector<LuaStateEntry>>::ok(lua_runtime_->entries());
    }
    return Result<std::vector<LuaStateEntry>>::fail(Error{"get-snapshot", "", errcat::kIO, "call from game thread"});
}

bool GameDispatcher::queue_plugin_chunk(std::string_view code) {
    if (code.empty()) return false;
    std::string code_copy(code);
    post([this, code = std::move(code_copy)]() -> Result<Value> {
        if (!lua_runtime_) return Result<Value>::fail(Error{"plugin-chunk", "", errcat::kIO, "no lua runtime set"});
        auto handle = lua_runtime_->first(LuaStateRole::Menu);
        if (!handle) return Result<Value>::fail(Error{"plugin-chunk", "", errcat::kMissing, "no menu state"});
        auto r = lua_runtime_->run_chunk_on_game_thread(handle, code, "");
        if (!r.ok()) return Result<Value>::fail(r.error());
        return Result<Value>::ok(Value::nil());
    });
    return true;
}

Result<Value> GameDispatcher::create_mod_env(const ModId& mod_id) {
    return call([this, &mod_id]() -> Result<Value> {
        if (!lua_runtime_) return Result<Value>::fail(Error{"create-mod-env", "", errcat::kIO, "no lua runtime set"});
        auto handle = lua_runtime_->first(LuaStateRole::Menu);
        if (!handle) return Result<Value>::fail(Error{"create-mod-env", "", errcat::kMissing, "no menu state"});
        return Result<Value>::fail(Error{"create-mod-env", "", errcat::kIO, "not implemented in core; see lua_bridge.cpp"});
    });
}

Result<void> GameDispatcher::install_c_function(LuaStateRole role,
                                                const char* name,
                                                void* c_fn) {
    if (!name || !c_fn) return Result<void>::fail(Error{"install-cfn", "", errcat::kType, "null name or fn"});

    // Inline queue logic for void return
    if (on_game_thread()) {
        if (!lua_runtime_) return Result<void>::fail(Error{"install-cfn", "", errcat::kIO, "no lua runtime set"});
        auto handle = lua_runtime_->first(role);
        if (!handle) return Result<void>::fail(Error{"install-cfn", "", errcat::kMissing, "no state for role"});
        return Result<void>::fail(Error{"install-cfn", "", errcat::kIO, "not implemented in core; see lua_bridge.cpp"});
    }

    // Off-thread: fire and forget, return success (caller can't wait for void)
    post([this, role, name, c_fn]() -> Result<Value> {
        if (!lua_runtime_) return Result<Value>::fail(Error{"install-cfn", "", errcat::kIO, "no lua runtime set"});
        auto handle = lua_runtime_->first(role);
        if (!handle) return Result<Value>::fail(Error{"install-cfn", "", errcat::kMissing, "no state for role"});
        return Result<Value>::fail(Error{"install-cfn", "", errcat::kIO, "not implemented in core; see lua_bridge.cpp"});
    });
    return Result<void>::success();
}

} // namespace ttmod