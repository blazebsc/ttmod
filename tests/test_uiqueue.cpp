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
    std::puts("uiqueue: all asserts passed");
    return 0;
}
