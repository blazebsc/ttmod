#include "ttmod/events.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using ttmod::classify_path;
    assert(classify_path("h:/g/archives/_resdesc_50_boot.lua") == "resdesc");
    assert(classify_path("h:/g/archives/_rescdesc_50_version_101.lua") == "resdesc"); // engine typo
    assert(classify_path("h:/g/archives/mcsm_pc_menu_ms.ttarch2") == "archive");
    assert(classify_path("c:/w/x.dll") == "other");
    assert(classify_path("_resdesc_50_x.lua") == "resdesc");
    assert(classify_path("h:/g/_resdesc_.lua.bak") == "other");
    assert(classify_path("h:/g/resdesc_50_x.lua") == "other"); // missing leading _

    ttmod::EventBus bus;
    assert(!bus.has(ttmod::EV_FILE_OPEN));
    int seen_file = 0, seen_arc = 0;
    int t1 = bus.subscribe(ttmod::EV_FILE_OPEN, [&](const ttmod::FileEvent& e) {
        seen_file++;
        assert(e.id == ttmod::EV_FILE_OPEN && e.category == "other");
    });
    int t2 = bus.subscribe(ttmod::EV_ARCHIVE_OPEN, [&](const ttmod::FileEvent& e) {
        seen_arc++;
        assert(e.category == "archive" && e.succeeded);
    });
    assert(bus.has(ttmod::EV_FILE_OPEN) && !bus.has(ttmod::EV_RESDESC_OPEN));
    ttmod::FileEvent a;
    a.id = ttmod::EV_FILE_OPEN;
    a.category = "other";
    bus.dispatch(a);
    ttmod::FileEvent b;
    b.id = ttmod::EV_ARCHIVE_OPEN;
    b.category = "archive";
    b.succeeded = true;
    bus.dispatch(b);
    bus.dispatch(b); // resdesc subscribers: none, no crash
    assert(seen_file == 1 && seen_arc == 2);
    bus.unsubscribe(t1);
    bus.dispatch(a);
    assert(seen_file == 1); // gone
    bus.unsubscribe(t2);
    assert(!bus.has(ttmod::EV_ARCHIVE_OPEN));
    ttmod::EventBus b2;
    assert(b2.snapshot(ttmod::EV_FILE_OPEN).empty());
    int seen2 = 0;
    b2.subscribe(ttmod::EV_FILE_OPEN, [&](const ttmod::FileEvent&) { seen2++; });
    auto snap = b2.snapshot(ttmod::EV_FILE_OPEN);
    assert(snap.size() == 1);
    snap[0](a); // invoke outside any lock, as the bridge does
    assert(seen2 == 1);
    (void)t1;
    std::puts("events: all asserts passed");
    return 0;
}
