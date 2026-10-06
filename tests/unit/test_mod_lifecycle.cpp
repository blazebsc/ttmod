// Mod lifecycle + scheduler + the Lua-free/VM-free guarantees they rest on.
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "ttmod/mod_lifecycle.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/scheduler.hpp"

using namespace ttmod;

static ModId mid(const char* s) {
    auto r = ModId::parse(s);
    assert(r.ok());
    return r.value();
}

int main() {
    // ---- lifecycle: the happy path -------------------------------------
    {
        auto s = ModRunState::Prepared;
        s = advance(s, "ok");
        assert(s == ModRunState::Running);
        s = advance(s, "stop");
        assert(s == ModRunState::Stopping);
        s = advance(s, "unloaded");
        assert(s == ModRunState::Unloaded);
    }
    // Failure is terminal: a failed mod cannot silently become Running again.
    // That loop is exactly the "broken mod retries forever" bug §63 rules out.
    {
        auto s = advance(ModRunState::Prepared, "fail");
        assert(s == ModRunState::Failed);
        assert(advance(s, "ok") == ModRunState::Failed);
        assert(advance(s, "unloaded") == ModRunState::Failed);
        assert(advance(ModRunState::Running, "fail") == ModRunState::Failed);
        // Unloaded is terminal too.
        assert(advance(ModRunState::Unloaded, "ok") == ModRunState::Unloaded);
        // Unknown events and impossible orders leave the state alone.
        assert(advance(ModRunState::Prepared, "unloaded") == ModRunState::Prepared);
        assert(advance(ModRunState::Prepared, "bogus") == ModRunState::Prepared);
        assert(advance(ModRunState::Prepared, nullptr) == ModRunState::Prepared);
    }
    // Failures are structured, not a bool, and say which stage broke.
    {
        Failure f;
        f.stage = ModStage::Compile;
        f.error = Error{"script", "main.lua:12", errcat::kSyntax, "unexpected symbol"};
        ModRunStatus st;
        st.id = "example";
        st.state = ModRunState::Failed;
        st.failure = f;
        st.errors = 3;
        assert(f.describe() == "compile: main.lua:12: unexpected symbol");
        assert(st.last_error_line().find("main.lua:12") == 0);
        assert(st.last_error_line().find("+2 more") != std::string::npos);
        // A running mod with no errors reports nothing.
        ModRunStatus ok;
        ok.state = ModRunState::Running;
        assert(ok.last_error_line().empty());
        assert(std::string(to_string(ModRunState::Running)) == "running");
        assert(std::string(to_string(ModStage::Entrypoint)) == "entrypoint");
    }

    // ---- scheduler: three queues ---------------------------------------
    ScriptScheduleOptions sopt;
    sopt.cascade_depth = 8;
    {
        ScriptScheduler sc(sopt);
        std::vector<int> order;
        auto a = mid("a.mod"), b = mid("b.mod");
        sc.defer_next(a, [&] {
            order.push_back(1);
            return Result<Value>::ok(Value::nil());
        });
        sc.defer_now(b, [&] {
            order.push_back(2);
            return Result<Value>::ok(Value::nil());
        });
        sc.defer_now(a, [&] {
            order.push_back(3);
            return Result<Value>::ok(Value::nil());
        });
        assert(sc.pending() == 3);
        assert(sc.run_once(0) == 3);
        // Next runs before Now; within a queue, insertion order holds.
        assert((order == std::vector<int>{1, 2, 3}));
        assert(sc.pending() == 0);
    }
    // Timers: one-shot fires once, interval re-arms.
    {
        ScriptScheduler sc(sopt);
        int once = 0, rep = 0;
        sc.at(
            mid("x"),
            [&] {
                ++once;
                return Result<Value>::ok(Value::nil());
            },
            100);
        sc.at(
            mid("x"),
            [&] {
                ++rep;
                return Result<Value>::ok(Value::nil());
            },
            100, 50);
        assert(sc.run_once(50) == 0);  // not due yet
        assert(sc.run_once(100) == 2); // both due
        assert(once == 1 && rep == 1);
        assert(sc.run_once(149) == 0);
        assert(sc.run_once(150) == 1); // interval re-armed at 100+50
        assert(once == 1 && rep == 2);
        // The one-shot does not repeat; the interval keeps its cadence.
        assert(sc.run_once(150) == 0); // already re-armed to 200
        assert(sc.run_once(200) == 1);
        assert(once == 1 && rep == 3);
    }
    // A self-deferring job is bounded by the cascade depth, not an infinite
    // spin. This is the "mod hangs the script thread" guard.
    {
        ScriptScheduleOptions tight;
        tight.cascade_depth = 5;
        ScriptScheduler sc(tight);
        int n = 0;
        // A job that re-defers ITSELF every pass: without the cascade bound this
        // never terminates.
        std::function<Result<Value>()> spin = [&] {
            ++n;
            sc.defer_now(mid("loop"), spin);
            return Result<Value>::ok(Value::nil());
        };
        sc.defer_now(mid("loop"), spin);
        sc.run_once(0);
        assert(n == 5); // exactly cascade_depth, no more
    }
    // §63 isolation: disarm_owner drops one mod's jobs and leaves others'.
    {
        ScriptScheduler sc(sopt);
        auto bad = mid("bad.mod"), good = mid("good.mod");
        sc.defer_now(bad, [] { return Result<Value>::ok(Value::nil()); });
        sc.defer_next(bad, [] { return Result<Value>::ok(Value::nil()); });
        sc.defer_now(good, [] { return Result<Value>::ok(Value::nil()); });
        assert(sc.pending() == 3);
        assert(sc.disarm_owner(bad) == 2);
        assert(sc.pending() == 1);
        assert(sc.run_once(0) == 1);
    }
    // cancel by id; cancelling twice or cancelling junk is safe.
    {
        ScriptScheduler sc(sopt);
        auto id = sc.defer_now(mid("a.mod"), [] { return Result<Value>::ok(Value::nil()); });
        assert(id.valid());
        assert(sc.cancel(id));
        assert(!sc.cancel(id));
        assert(!sc.cancel(TaskId{}));
        assert(!sc.cancel(TaskId{9999}));
        assert(sc.pending() == 0);
    }
    // A job may schedule more work and cancel itself without deadlocking:
    // jobs run outside the lock.
    {
        ScriptScheduler sc(sopt);
        int ran = 0;
        auto id = sc.defer_next(mid("re"), [&] {
            ++ran;
            sc.defer_now(mid("re"), [&] {
                ++ran;
                return Result<Value>::ok(Value::nil());
            });
            return Result<Value>::ok(Value::nil());
        });
        assert(sc.run_once(0) >= 1);
        assert(ran >= 2);
        (void)id;
    }
    // stop() releases the wait and refuses new work.
    {
        ScriptScheduler sc(sopt);
        sc.defer_now(mid("a.mod"), [] { return Result<Value>::ok(Value::nil()); });
        sc.stop();
        assert(sc.stopped());
        assert(sc.pending() == 0);
        assert(!sc.wait_and_run(0));
        assert(!sc.defer_now(mid("a.mod"), [] { return Result<Value>::ok(Value::nil()); }).valid());
        sc.stop(); // idempotent
    }
    // wait_and_run returns promptly when a job is queued from another thread.
    {
        ScriptScheduler sc(sopt);
        int ran = 0;
        std::thread t([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            sc.defer_now(mid("w"), [&] {
                ++ran;
                return Result<Value>::ok(Value::nil());
            });
        });
        uint64_t now = 0;
        while (ran == 0 && sc.wait_and_run(now += 5)) {
        }
        t.join();
        assert(ran == 1);
    }

    std::puts("mod_lifecycle: all asserts passed");
    return 0;
}