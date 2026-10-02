#include "ttmod/modstate.hpp"
#include <cassert>
#include <cstdio>

int main() {
    auto blank = ttmod::parse_state("");
    assert(blank.ok && blank.state.overrides.empty());
    auto ws = ttmod::parse_state("  \n ");
    assert(ws.ok);

    auto good = ttmod::parse_state("{\"a.mod\": {\"enabled\": false}, \"b.mod\":{\"enabled\":true}}");
    assert(good.ok);
    assert(!good.state.enabled_for("a.mod", true));
    assert(good.state.enabled_for("b.mod", false));
    assert(good.state.enabled_for("c.mod", true));  // missing -> manifest default
    assert(!good.state.enabled_for("c.mod", false));

    auto bad = ttmod::parse_state("{oops");
    assert(!bad.ok);
    auto bad2 = ttmod::parse_state("{\"a\":{\"enabled\":\"yes\"}}");
    assert(!bad2.ok);

    // unknown fields tolerated, entry without enabled ignored
    auto fwd = ttmod::parse_state("{\"a\":{\"enabled\":false,\"future\":[1,2]},\"b\":{}}");
    assert(fwd.ok && !fwd.state.enabled_for("a", true));
    assert(fwd.state.enabled_for("b", true)); // no enabled -> default

    // serialize deterministic + round-trip
    ttmod::ModState st;
    st.set("b.mod", true);
    st.set("a.mod", false);
    std::string s = st.serialize();
    assert(s.find("\"a.mod\"") < s.find("\"b.mod\"")); // sorted
    auto rt = ttmod::parse_state(s);
    assert(rt.ok && !rt.state.enabled_for("a.mod", true) && rt.state.enabled_for("b.mod", false));
    std::puts("modstate: all asserts passed");
    return 0;
}
