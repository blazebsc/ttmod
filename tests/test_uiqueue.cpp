// uiqueue: push/take/overflow/empty semantics for the v5 plugin chunk queue.
#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/uiqueue.hpp"

int main() {
    assert(ttmod::uiqueue_take().empty());
    assert(!ttmod::uiqueue_push("")); // empty input rejected
    assert(ttmod::uiqueue_push("a = 1"));
    assert(ttmod::uiqueue_push("b = 2"));
    auto q = ttmod::uiqueue_take();
    assert(q.size() == 2 && q[0] == "a = 1" && q[1] == "b = 2");
    assert(ttmod::uiqueue_take().empty()); // drained
    // capacity: 16, overflow drops
    for (int i = 0; i < 20; ++i) {
        char buf[32];
        snprintf(buf, sizeof buf, "c%d = %d", i, i);
        if (i < 16) assert(ttmod::uiqueue_push(buf));
        else assert(!ttmod::uiqueue_push(buf)); // dropped, not grown
    }
    q = ttmod::uiqueue_take();
    assert(q.size() == 16 && q[15] == "c15 = 15");
    assert(ttmod::uiqueue_take().empty());
    // Replay history survives the drain: the menu runs in a DIFFERENT lua_State
    // than the script thread, so globals set during the drain must be replayed
    // there. take() must NOT consume the history, or menu replay has nothing to
    // play (this is what left the menu unthemed on 2026-10-03).
    auto h = ttmod::uiqueue_recent();
    assert(!h.empty());
    assert(h.back() == "c15 = 15");
    assert(std::string(h.back()).find("c15 = 15") == 0);
    assert(ttmod::uiqueue_recent().size() == h.size()); // repeatable, not drained
    assert(ttmod::uiqueue_take().empty());               // still drained
    // Typed commands: ExecuteChunk round-trips; reserved types rejected.
    assert(ttmod::uiqueue_push_cmd({ttmod::UiCommandType::ExecuteChunk, "t = 1"}));
    assert(!ttmod::uiqueue_push_cmd({ttmod::UiCommandType::RefreshMenu, ""}));
    assert(!ttmod::uiqueue_push_cmd({ttmod::UiCommandType::SetValue, "x"}));
    auto tc = ttmod::uiqueue_take_cmd();
    assert(tc.size() == 1 && tc[0].type == ttmod::UiCommandType::ExecuteChunk && tc[0].code == "t = 1");
    assert(ttmod::uiqueue_take_cmd().empty());
    std::puts("uiqueue: all asserts passed");
    return 0;
}
