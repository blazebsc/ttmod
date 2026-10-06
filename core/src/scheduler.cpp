#include "ttmod/scheduler.hpp"

#include <algorithm>
#include <chrono>

namespace ttmod {

ScriptScheduler::ScriptScheduler(ScriptScheduleOptions opt) : opt_(opt) {}

ScriptScheduler::~ScriptScheduler() {
    stop();
}

TaskId ScriptScheduler::defer_now(const ModId& owner, Job j) {
    if (!j) return TaskId{};
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return TaskId{};
    Entry e;
    e.id = TaskId{next_id_++};
    e.owner = owner;
    e.job = std::move(j);
    e.seq = seq_++;
    now_.push_back(e);
    cv_.notify_all();
    return e.id;
}

TaskId ScriptScheduler::defer_next(const ModId& owner, Job j) {
    if (!j) return TaskId{};
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return TaskId{};
    Entry e;
    e.id = TaskId{next_id_++};
    e.owner = owner;
    e.job = std::move(j);
    e.seq = seq_++;
    next_.push_back(e);
    cv_.notify_all();
    return e.id;
}

TaskId ScriptScheduler::at(const ModId& owner, Job j, uint64_t delay_ms, uint32_t period_ms) {
    if (!j) return TaskId{};
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return TaskId{};
    Entry e;
    e.id = TaskId{next_id_++};
    e.owner = owner;
    e.job = std::move(j);
    e.due_ms = delay_ms;
    e.period_ms = period_ms;
    e.seq = seq_++;
    at_.push_back(e);
    cv_.notify_all();
    return e.id;
}

bool ScriptScheduler::cancel(TaskId id) {
    if (!id.valid()) return false;
    std::lock_guard<std::mutex> lock(mtx_);
    // Scan rather than index: the queues hold tens of entries, and an index of
    // pointers into a vector dangles on every reallocation.
    for (auto* q : {&now_, &next_, &at_}) {
        auto it = std::find_if(q->begin(), q->end(), [&](const Entry& e) { return e.id == id; });
        if (it != q->end()) {
            q->erase(it);
            return true;
        }
    }
    return false;
}

size_t ScriptScheduler::disarm_owner(const ModId& owner) {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t n = 0;
    for (auto* q : {&now_, &next_, &at_}) {
        size_t before = q->size();
        q->erase(std::remove_if(q->begin(), q->end(), [&](const Entry& e) { return e.owner == owner; }), q->end());
        n += before - q->size();
    }
    return n;
}

void ScriptScheduler::cancel_owner(const ModId& owner) {
    disarm_owner(owner);
}

size_t ScriptScheduler::run_once(uint64_t now_ms) {
    size_t ran = 0;
    // Bounded pass count: a job that defers itself every pass would otherwise
    // spin forever on the script thread. The cascade bound is not preemption;
    // a genuinely runaway job is bounded by the VM instruction budget.
    for (int pass = 0; pass < opt_.cascade_depth; ++pass) {
        std::vector<Entry> due;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stopped_) break;
            // Next before Now: work queued for the next pass runs ahead of this
            // pass's trailing defers. That ordering IS the difference between
            // the two queues.
            due.insert(due.end(), next_.begin(), next_.end());
            next_.clear();

            // Timers: partition at_ into "fired now" and "still waiting", rearming
            // intervals in place. One pass, no tombstones: a copy-rearm leaves
            // the queue entry on its old deadline and it re-fires every pass.
            std::vector<Entry> waiting;
            for (auto& e : at_) {
                if (e.due_ms > now_ms) {
                    waiting.push_back(e);
                    continue;
                }
                if (e.period_ms > 0) {
                    e.due_ms = now_ms + e.period_ms;
                    waiting.push_back(e);
                }
                due.push_back(e); // one-shot: not re-added; interval: rearmed copy
            }
            // Fair timer order: earliest deadline, then insertion order.
            std::sort(due.begin(), due.end(), [](const Entry& a, const Entry& b) {
                if (a.due_ms != b.due_ms) return a.due_ms < b.due_ms;
                return a.seq < b.seq;
            });
            at_.swap(waiting);

            due.insert(due.end(), now_.begin(), now_.end());
            now_.clear();
        }
        if (due.empty()) break;
        for (auto& e : due) {
            // Outside the lock: a job may schedule more work, cancel itself, or
            // re-enter the host. A failing job is its owner's problem - the
            // scheduler's contract is "run it", not "judge it" (§63).
            if (e.job) (void)e.job();
            ++ran;
        }
    }
    return ran;
}

bool ScriptScheduler::wait_and_run(uint64_t now_ms) {
    {
        std::unique_lock<std::mutex> lock(mtx_);
        if (stopped_) return false;
        auto due = [&] {
            if (!next_.empty() || !now_.empty()) return true;
            for (auto& e : at_)
                if (e.due_ms <= now_ms) return true;
            return false;
        };
        if (!due()) {
            cv_.wait_for(lock, std::chrono::milliseconds(opt_.tick_ms),
                         [&] { return stopped_ || !next_.empty() || !now_.empty(); });
            if (stopped_) return false;
        }
    }
    run_once(now_ms);
    return true;
}

void ScriptScheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) return;
        stopped_ = true;
        now_.clear();
        next_.clear();
        at_.clear();
    }
    cv_.notify_all();
}

size_t ScriptScheduler::pending() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return now_.size() + next_.size() + at_.size();
}

bool ScriptScheduler::stopped() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return stopped_;
}

} // namespace ttmod