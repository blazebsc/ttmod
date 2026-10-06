#pragma once
// Script scheduler (doc §§62, 63).
//
// The minimum useful set, and it is three queues:
//
//   Now - run at the end of the current pass, including ones added during
//         the pass, bounded so a self-deferring mod cannot hang the thread.
//   Next - run at the start of the next pass.
//   At   - deadline timers; period 0 = one-shot, period > 0 = interval.
//
// Deliberately NOT here: coroutines, promises, channels, per-mod tick counts.
// They are language features, not framework facilities, and each one is a
// second subsystem nobody has asked for yet.
//
// The critical principle from the doc: TTMod scheduling and game-thread
// scheduling are DIFFERENT concepts. Nothing in this file touches the game;
// a job that needs the game goes through GameDispatcher.
//
// A Job is a closure that returns a Result. It knows about the VM; this
// scheduler does not, which is what lets it be tested with no VM present.
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ttmod/modid.hpp"
#include "ttmod/result.hpp"
#include "ttmod/script_value.hpp"

namespace ttmod {

struct TaskId {
    uint32_t v = 0;
    [[nodiscard]] bool valid() const noexcept {
        return v != 0;
    }
    bool operator==(const TaskId& o) const noexcept {
        return v == o.v;
    }
};

struct ScriptScheduleOptions {
    // Wakeup granularity for timers. NOT a script-visible tick: nothing in the
    // script API exposes "tick N".
    uint64_t tick_ms = 16;
    // ponytail: bounds self-defer (a job that re-defers itself), NOT
    // preemption. A genuinely runaway job is bounded by the VM's instruction
    // budget, which is a different mechanism and a backend concern.
    int cascade_depth = 32;
};

class ScriptScheduler {
  public:
    using Job = std::function<Result<Value>()>;

    explicit ScriptScheduler(ScriptScheduleOptions opt = ScriptScheduleOptions{});
    ~ScriptScheduler();

    ScriptScheduler(const ScriptScheduler&) = delete;
    ScriptScheduler& operator=(const ScriptScheduler&) = delete;

    TaskId defer_now(const ModId& owner, Job j);
    TaskId defer_next(const ModId& owner, Job j);
    TaskId at(const ModId& owner, Job j, uint64_t delay_ms, uint32_t period_ms = 0);

    bool cancel(TaskId id);
    void cancel_owner(const ModId& owner);
    // §63: isolate a failing mod, do not kill the runtime. Drops every pending
    // job belonging to the owner and returns how many went.
    size_t disarm_owner(const ModId& owner);

    // One pass: Now -> Next -> At, each in insertion order. Jobs run OUTSIDE
    // the internal lock (snapshot first), exactly like EventBus, so a job may
    // schedule more work or call back into the host.
    size_t run_once(uint64_t now_ms);
    // Blocks until a job is due, the tick elapses, or stop() is called.
    // Returns false when stopping (the pass has already run).
    bool wait_and_run(uint64_t now_ms);
    void stop();

    [[nodiscard]] size_t pending() const;
    [[nodiscard]] bool stopped() const;

  private:
    struct Entry {
        TaskId id;
        ModId owner;
        Job job;
        uint64_t due_ms = 0;
        uint32_t period_ms = 0;
        uint64_t seq = 0; // insertion order, for determinism
    };

    ScriptScheduleOptions opt_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<Entry> now_, next_, at_;
    uint32_t next_id_ = 1;
    uint64_t seq_ = 0;
    bool stopped_ = false;
};

} // namespace ttmod