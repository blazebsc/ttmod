#include "ttmod/modstate.hpp"
#include <cassert>
#include <cstdio>

namespace {

ttmod::ModId mid(const char* id) {
    return ttmod::ModId::parse(id).value();
}

} // namespace

int main() {
    auto blank = ttmod::parse_state("");
    assert(blank.ok() && blank.value().overrides.empty());
    auto ws = ttmod::parse_state("  \n ");
    assert(ws.ok());

    auto good = ttmod::parse_state("{\"a.mod\": {\"enabled\": false}, \"b.mod\":{\"enabled\":true}}");
    assert(good.ok());
    assert(!good.value().enabled_for(mid("a.mod"), true));
    assert(good.value().enabled_for(mid("b.mod"), false));
    assert(good.value().enabled_for(mid("c.mod"), true));  // missing -> manifest default
    assert(!good.value().enabled_for(mid("c.mod"), false));

    auto bad = ttmod::parse_state("{oops");
    assert(!bad.ok());
    auto bad2 = ttmod::parse_state("{\"a\":{\"enabled\":\"yes\"}}");
    assert(!bad2.ok());

    // unknown fields tolerated, entry without enabled ignored
    auto fwd = ttmod::parse_state("{\"a\":{\"enabled\":false,\"future\":[1,2]},\"b\":{}}");
    assert(fwd.ok() && !fwd.value().enabled_for(mid("a"), true));
    assert(fwd.value().enabled_for(mid("b"), true)); // no enabled -> default

    // serialize deterministic + round-trip
    ttmod::ModState st;
    st.set(mid("b.mod"), true);
    st.set(mid("a.mod"), false);
    std::string s = st.serialize();
    assert(s.find("\"a.mod\"") < s.find("\"b.mod\"")); // sorted
    auto rt = ttmod::parse_state(s);
    assert(rt.ok() && !rt.value().enabled_for(mid("a.mod"), true) &&
           rt.value().enabled_for(mid("b.mod"), false));

    auto rejected = ttmod::parse_state(
        "{\"a..b\":{\"enabled\":false},\"good.mod\":{\"enabled\":true}}");
    assert(rejected.ok() && rejected.value().rejected_keys.size() == 1 &&
           rejected.value().rejected_keys[0] == "a..b");
    assert(rejected.value().overrides.size() == 1 &&
           rejected.value().enabled_for(mid("good.mod"), false));
    const std::string cleaned = rejected.value().serialize();
    assert(cleaned.find("a..b") == std::string::npos);
    auto cleaned_round_trip = ttmod::parse_state(cleaned);
    assert(cleaned_round_trip.ok() && cleaned_round_trip.value().rejected_keys.empty() &&
           cleaned_round_trip.value().enabled_for(mid("good.mod"), false));
    std::puts("modstate: all asserts passed");
    return 0;
}
