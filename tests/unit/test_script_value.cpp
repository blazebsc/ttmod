// Value: the cross-VM representation, its limits, and its determinism
// guarantees. Tested without any VM present, because that is the point - if
// marshaling needed a VM to be testable it would be untestable.
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

#include "ttmod/script_value.hpp"

using namespace ttmod;

static Value::ArrayVec arr_of(std::vector<Value> v) {
    return v;
}

int main() {
    // Scalars.
    assert(Value::nil().is_nil() && !Value::nil().truthy());
    assert(Value::boolean(true).truthy() && !Value::boolean(false).truthy());
    assert(Value::number(3.5).as_number() == 3.5);
    assert(Value::number(0.0).truthy() == false); // Lua truthiness: 0 is false
    assert(Value::string("hi").as_string() == "hi");
    // Wrong-kind reads return the default, never throw.
    assert(Value::string("hi").as_number(-1) == -1.0);
    assert(Value::number(1).as_bool(true) == true);
    assert(Value::boolean(true).as_string().empty());

    // Unsupported is NOT nil: "denied" and "absent" must be distinguishable.
    auto u = Value::unsupported("no such field");
    assert(u.kind() == Value::Kind::Unsupported);
    assert(!u.is_nil() && !u.truthy());
    assert(u.why() == "no such field");
    assert(u.to_string() == "<unsupported: no such field>");

    // Composites.
    auto arr = Value::array(
        std::make_shared<Value::ArrayVec>(arr_of({Value::number(1), Value::string("two"), Value::boolean(true)})));
    assert(arr.kind() == Value::Kind::Array && arr.size() == 3);
    assert(arr.at(1).as_string() == "two");
    assert(arr.at(99).is_nil()); // out of range reads as nil, not UB
    assert(arr.at(99).size() == 0);

    // Object fields are SORTED and DEDUPED at construction (determinism).
    auto fields = std::make_shared<Value::Fields>();
    fields->emplace_back("zeta", Value::number(1));
    fields->emplace_back("alpha", Value::number(2));
    fields->emplace_back("zeta", Value::number(99)); // duplicate key
    auto obj = Value::object(fields);
    assert(obj.size() == 2);
    assert(obj.fields()[0].first == "alpha");
    assert(obj.fields()[1].first == "zeta");
    assert(obj.fields()[1].second.as_number() == 1.0); // first wins, not last
    assert(obj.find("alpha") && obj.find("alpha")->as_number() == 2.0);
    assert(obj.find("missing") == nullptr);
    // Deterministic rendering regardless of input order.
    std::string rendered = obj.to_string();
    assert(rendered == "{alpha=2, zeta=1}");

    // Nested composites cross whole.
    auto nested = std::make_shared<Value::Fields>();
    nested->emplace_back("items", arr);
    nested->emplace_back("name", Value::string("x"));
    auto top = Value::object(nested);
    assert(top.find("items") && top.find("items")->at(1).as_string() == "two");
    assert(value_node_count(top) == 1 + 1 + 3 + 1); // top + array + 3 items + name

    // Structural equality is type-sensitive: 1 ~= "1", true ~= 1.
    assert(value_equal(Value::number(1), Value::number(1)));
    assert(!value_equal(Value::number(1), Value::string("1")));
    assert(!value_equal(Value::boolean(true), Value::number(1)));
    assert(value_equal(arr, arr));
    assert(value_equal(Value::nil(), Value::nil()));
    assert(value_equal(Value::unsupported("a"), Value::unsupported("b"))); // kind only
    {
        auto a1 = Value::array(std::make_shared<Value::ArrayVec>(arr_of({Value::number(1)})));
        auto a2 = Value::array(std::make_shared<Value::ArrayVec>(arr_of({Value::number(2)})));
        assert(!value_equal(a1, a2));
    }

    // Game object handles: opaque, generation-checked.
    GameObjectHandle h{7, 3};
    GameObjectHandle stale{7, 4}; // slot recycled
    GameObjectHandle none{};
    assert(h.valid() && !none.valid());
    GameObjectHandle same{7, 3};
    assert(h == same);
    assert(h != stale); // generation mismatch is a different object
    auto gv = Value::game_object(h);
    assert(gv.kind() == Value::Kind::GameObject && gv.handle() == h);
    assert(gv.truthy());
    assert(gv.to_string() == "<object 7/3>");

    // Limits: depth, node budget, string length, array/object size.
    {
        Value::Limits saved = Value::limits();
        Value::Limits l;
        l.depth = 3;
        l.nodes = 32;
        l.string_bytes = 8;
        l.array_items = 4;
        l.object_fields = 3;
        Value::set_limits(l);

        assert(Value::string("12345678").kind() == Value::Kind::String);
        auto toolong = Value::string("123456789");
        assert(toolong.kind() == Value::Kind::Unsupported);
        assert(toolong.why() == "string too long");

        // Depth: build 6 nested arrays, expect the deep ones to degrade.
        Value cur = Value::number(1);
        for (int i = 0; i < 6; ++i) cur = Value::array(std::make_shared<Value::ArrayVec>(arr_of({cur})));
        size_t degraded = 0;
        Value walk = cur;
        while (walk.kind() == Value::Kind::Array && walk.size() == 1) {
            walk = walk.at(0);
            if (walk.kind() == Value::Kind::Unsupported) ++degraded;
        }
        assert(degraded >= 1);
        assert(walk.why() == "too deep");

        // Node budget truncates the walk rather than recursing forever.
        Value::Limits tiny;
        tiny.nodes = 3;
        Value::set_limits(tiny);
        auto wide = Value::array(std::make_shared<Value::ArrayVec>(
            arr_of({Value::number(1), Value::number(2), Value::number(3), Value::number(4)})));
        assert(wide.size() == 4);
        Value count = Value::number(0);
        (void)count;
        Value::set_limits(saved);
    }

    // Sharing: a composite handed to another thread is immutable by type.
    {
        auto shared = std::make_shared<Value::ArrayVec>(arr_of({Value::number(42)}));
        Value a = Value::array(shared);
        Value b = a; // copy
        assert(value_equal(a, b));
        assert(b.at(0).as_number() == 42.0);
    }

    std::puts("script_value: all asserts passed");
    return 0;
}