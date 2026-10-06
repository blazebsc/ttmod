// GameDispatcher: thread policy, timeout, drop accounting, shutdown. Runs with
// no game and no Windows, which is the point - the thread discipline must be
// provable before it is wrapped around a real lua_State.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>

#include "ttmod/game_dispatch.hpp"

using namespace ttmod;

int main() {
    GameDispatcher::Options opt;
    opt.timeout = std::chrono::milliseconds(300);
    opt.capacity = 4;

    // On the game thread, call() runs inline: no queueing, no deadlock.
    {
        GameDispatcher d(std::this_thread::get_id(), opt);
        assert(d.on_game_thread());
        auto r = d.call([] { return Result<Value>::ok(Value::string("inline")); });
        assert(r.ok() && r.value().as_string() == "inline");
        assert(d.pending() == 0);
    }

    // Off the game thread: enqueue, block, and the game thread's pump answers.
    {
        GameDispatcher d(std::this_thread::get_id(), opt);
        std::atomic<bool> ran{false};
        std::thread worker([&] {
            auto r = d.call([&] {
                ran = true;
                return Result<Value>::ok(Value::number(42));
            });
            assert(r.ok());
            assert(r.value().as_number() == 42.0);
        });
        // Give the worker time to enqueue, then service it as the game thread.
        while (d.pending() == 0) std::this_thread::yield();
        assert(d.pump() == 1);
        worker.join();
        assert(ran);
    }

    // An op that returns an error propagates the error, not an exception and
    // not a silent nil.
    {
        GameDispatcher d(std::this_thread::get_id(), opt);
        std::thread worker([&] {
            auto r = d.call([] { return Result<Value>::fail(Error{"x", "", errcat::kIO, "nope"}); });
            assert(!r.ok());
            assert(r.error().message == "nope");
        });
        while (d.pending() == 0) std::this_thread::yield();
        d.pump();
        worker.join();
    }

    // Ops run in arrival order under a single producer (the FIFO guarantee).
    {
        GameDispatcher d(std::this_thread::get_id(), opt);
        std::string order;
        std::thread worker([&] {
            for (int i = 0; i < 3; ++i)
                d.post([&order, i] {
                    order += std::to_string(i);
                    return Result<Value>::ok(Value::nil());
                });
        });
        worker.join();
        d.pump();
        assert(order == "012");
    }

    // Overflow is dropped and COUNTED, never silently lost, never unbounded.
    {
        GameDispatcher d(std::this_thread::get_id(), opt); // capacity 4
        for (int i = 0; i < 10; ++i) d.post([] { return Result<Value>::ok(Value::nil()); });
        assert(d.pending() == 4);
        assert(d.dropped() == 6);
    }

    // Timeout returns a structured error instead of hanging the script thread.
    {
        GameDispatcher::Options tight;
        tight.timeout = std::chrono::milliseconds(30);
        GameDispatcher d(std::this_thread::get_id(), tight);
        bool got_error = false;
        std::thread worker([&] {
            auto r = d.call([] { return Result<Value>::ok(Value::nil()); });
            got_error = !r.ok();
            assert(r.error().category == ttmod::errcat::kIO);
        });
        worker.join();
        assert(got_error);
        // The abandoned op is still run by a later pump, and nobody is hurt:
        // the result is written into a waiter the caller already released.
        d.pump();
    }

    // Shutdown releases blocked callers immediately rather than making them
    // wait out their timeout.
    {
        GameDispatcher::Options slow;
        slow.timeout = std::chrono::seconds(30);
        GameDispatcher d(std::this_thread::get_id(), slow);
        std::atomic<bool> released{false};
        std::thread worker([&] {
            auto r = d.call([] { return Result<Value>::ok(Value::nil()); });
            released = !r.ok();
        });
        while (d.pending() == 0) std::this_thread::yield();
        d.shutdown();
        worker.join();
        assert(released);
        assert(d.pending() == 0);
        // Post after shutdown is a no-op, not a queue growth.
        d.post([] { return Result<Value>::ok(Value::nil()); });
        assert(d.pending() == 0);
    }

    // Values cross as Values, never as VM objects.
    {
        GameDispatcher d(std::this_thread::get_id(), opt);
        auto r = d.call([] {
            auto f = std::make_shared<Value::Fields>();
            f->emplace_back("scene", Value::string("chapter1"));
            return Result<Value>::ok(Value::object(f));
        });
        assert(r.ok());
        assert(r.value().find("scene") && r.value().find("scene")->as_string() == "chapter1");
    }

    std::puts("game_dispatch: all asserts passed");
    return 0;
}