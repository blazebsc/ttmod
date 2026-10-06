#include "ttmod/game_dispatch.hpp"

#include <memory>

namespace ttmod {

GameDispatcher::GameDispatcher(std::thread::id game_thread, Options opt) : game_thread_(game_thread), opt_(opt) {}

GameDispatcher::~GameDispatcher() {
    shutdown();
}

bool GameDispatcher::on_game_thread() const {
    return std::this_thread::get_id() == game_thread_;
}

Result<Value> GameDispatcher::call(Op op) {
    if (!op) return Result<Value>::fail(Error{"dispatch", "", errcat::kType, "null op"});
    // Fast path: already on the game thread. Queueing here would deadlock
    // against our own pump.
    if (on_game_thread()) return op();

    // shared_ptr, not a raw Waiter: if we time out while the op is still
    // queued, the queue still owns it, so a late completion writes into live
    // memory and the object dies only when the last reference goes.
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
            ++dropped_; // counted, never silent
            return;
        }
        queue_.push(Task{std::move(op), nullptr});
    }
    cv_.notify_all();
}

size_t GameDispatcher::pump() {
    // Drain a snapshot: ops queued during this pass wait for the next one, so a
    // self-requeueing op cannot hang the game thread.
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
        // Hand the result back, if anyone is still waiting for it. A caller
        // that already timed out simply never reads it.
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
    // Release every blocked caller with a structured error rather than
    // letting it sit until its timeout.
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

} // namespace ttmod