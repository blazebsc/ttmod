// ScriptVm contract + ScriptApiRegistry: the answer to "how does a function
// get into the VM, and what happens when its owner goes away".
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ttmod/script_api.hpp"
#include "ttmod/script_vm.hpp"

using namespace ttmod;

int main() {
    // ---- ApiKey --------------------------------------------------------
    {
        auto k = parse_api_key("events.on");
        assert(k.ok() && k.value().ns == "events" && k.value().name == "on");
        assert(k.value().dotted() == "events.on");
        assert(!parse_api_key("nodot").ok());
        assert(!parse_api_key(".leading").ok());
        assert(!parse_api_key("trailing.").ok());
        assert(!parse_api_key("a.b.c").ok()); // ambiguous, rejected
    }

    // ---- registry: basics ----------------------------------------------
    ScriptApiRegistry reg;
    ApiOwner fw{};
    ApiKey log_info{"log", "info"};
    auto tok = reg.register_fn(
        fw, log_info, [](CallCtx& ctx) { return Result<Value>::ok(Value::string(ctx.arg_str(0))); }, 1, 0, 1);
    assert(tok.ok());
    assert(reg.size() == 1);
    assert(reg.find(log_info) != nullptr);
    assert(reg.find(ApiKey{"log", "missing"}) == nullptr);

    // Dispatch reaches the function and marshals Values.
    Value args1[2] = {Value::string("hello"), Value::number(1)};
    {
        auto r = reg.invoke(tok.value(), args1, 1, &fw);
        assert(r.ok() && r.value().as_string() == "hello");
    }
    // Arity is checked before the call: a binding reading past nargs would read
    // whatever the stack happened to hold.
    {
        ApiKey needs_one{"game", "get"};
        auto t = reg.register_fn(fw, needs_one, [](CallCtx&) { return Result<Value>::ok(Value::nil()); }, 1, 1, 1);
        assert(t.ok());
        auto too_few = reg.invoke(t.value(), nullptr, 0, &fw);
        assert(!too_few.ok() && too_few.error().category == errcat::kRange);
        auto too_many = reg.invoke(t.value(), args1, 2, &fw);
        assert(!too_many.ok() && too_many.error().category == errcat::kRange);
        // log.info takes 0..1, so a 0-arg call is legal, not a range error.
        assert(reg.invoke(tok.value(), nullptr, 0, &fw).ok());
    }
    // Duplicate registration is refused, not silently overwritten.
    {
        auto dup = reg.register_fn(fw, log_info, [](CallCtx&) { return Result<Value>::ok(Value::nil()); }, 1, 0, -1);
        assert(!dup.ok() && dup.error().category == errcat::kDuplicate);
    }
    // A binding that requires a newer framework is refused here, not at the
    // moment a script calls it.
    {
        ApiKey future{"future", "thing"};
        auto bad = reg.register_fn(
            fw, future, [](CallCtx&) { return Result<Value>::ok(Value::nil()); }, (uint32_t)kApiVersion + 1, 0, 0);
        assert(!bad.ok() && bad.error().category == errcat::kRange);
    }
    // Null function and bad arity are rejected.
    {
        assert(!reg.register_fn(fw, ApiKey{"x", "y"}, nullptr, 1, 0, 0).ok());
        assert(!reg.register_fn(
                       fw, ApiKey{"x", "z"}, [](CallCtx&) { return Result<Value>::ok(Value::nil()); }, 1, 5, 2)
                    .ok());
        assert(!reg.register_fn(
                       fw, ApiKey{"", "y"}, [](CallCtx&) { return Result<Value>::ok(Value::nil()); }, 1, 0, 0)
                    .ok());
    }

    // ---- the dangling-callback answer ----------------------------------
    // Revoking tombstones the entry: the installed stub keeps its token, and
    // later calls fail structurally instead of jumping into freed memory.
    {
        bool called = false;
        ApiKey revocable{"log", "warn"};
        auto t = reg.register_fn(
            fw, revocable,
            [&](CallCtx&) {
                called = true;
                return Result<Value>::ok(Value::nil());
            },
            1, 0, 0);
        assert(t.ok());
        assert(reg.invoke(t.value(), nullptr, 0, &fw).ok() && called);
        size_t before = reg.size();
        reg.unregister(t.value());
        auto after = reg.invoke(t.value(), nullptr, 0, &fw);
        assert(!after.ok());
        assert(after.error().category == errcat::kMissing);
        called = false;
        auto revoked = reg.invoke(t.value(), nullptr, 0, &fw);
        assert(!revoked.ok()); // must not run the revoked fn
        assert(!called);
        assert(reg.find(revocable) == nullptr); // gone from lookup
        assert(reg.size() == before - 1);       // tombstoned: removed from the key index
        // Unregistering twice is safe.
        reg.unregister(t.value());
    }

    // ---- plugin namespacing --------------------------------------------
    // A plugin physically cannot squat a stable namespace: the registry
    // re-homes its keys under native/<plugin-id> regardless of what it asks
    // for. This is why "the plugin overwrote ttmod.game" is unrepresentable.
    {
        ApiOwner plug{ApiOwnerKind::Plugin, "graphics"};
        auto t = reg.register_fn(
            plug, ApiKey{"game", "draw"}, // tries to squat "game"
            [](CallCtx&) { return Result<Value>::ok(Value::string("drew")); }, 1, 0, 0);
        assert(t.ok());
        ApiKey rehomed{"native/graphics", "draw"};
        assert(reg.find(rehomed) != nullptr);
        assert(reg.find(ApiKey{"game", "draw"}) == nullptr);
        auto r = reg.invoke(t.value(), nullptr, 0, &plug);
        assert(r.ok() && r.value().as_string() == "drew");
        // Two plugins with the same requested key do not collide.
        ApiOwner other{ApiOwnerKind::Plugin, "audio"};
        auto t2 = reg.register_fn(
            other, ApiKey{"game", "draw"}, [](CallCtx&) { return Result<Value>::ok(Value::nil()); }, 1, 0, 0);
        assert(t2.ok());
    }

    // unregister_owner drops every entry of one owner and leaves others alone.
    {
        size_t before = reg.size();
        ApiOwner plug{ApiOwnerKind::Plugin, "graphics"};
        reg.unregister_owner(plug);
        assert(reg.size() < before);
        assert(reg.invoke(tok.value(), nullptr, 0, &fw).ok()); // framework entry survives
    }

    // A binding that throws is contained at the boundary: core has no
    // exceptions, so a plugin bug must not cross the ABI.
    {
        ApiKey boom{"log", "boom"};
        auto t =
            reg.register_fn(fw, boom, [](CallCtx&) -> Result<Value> { throw std::runtime_error("kaboom"); }, 1, 0, 0);
        assert(t.ok());
        auto r = reg.invoke(t.value(), nullptr, 0, &fw);
        assert(!r.ok());
        assert(r.error().message.find("plugin threw") != std::string::npos);
    }

    // keys() is sorted, so it is deterministic.
    {
        auto ks = reg.keys();
        for (size_t i = 1; i < ks.size(); ++i) assert(ks[i - 1] < ks[i]);
    }

    // ---- the Null backend ----------------------------------------------
    // It implements the contract faithfully and runs nothing: it is both the
    // disabled runtime and the test double.
    {
        ScriptApiRegistry api;
        auto vm = make_null_vm(api);
        assert(std::string(vm->backend()) == "null");
        assert(vm->bytes_used() == 0);
        auto r = vm->run_chunk("return 1", "main.lua", Value::nil());
        assert(!r.ok());
        assert(r.error().object == "main.lua");
        assert(!vm->run_chunk("", "empty", Value::nil()).ok()); // empty chunk is a real error
        assert(vm->install_api(1, ApiKey{"log", "x"}, 1).ok()); // accepts, executes nothing
        assert(!vm->call(CallbackRef{1}, nullptr, 0).ok());
        vm->release(CallbackRef{1}); // no-op, must not crash
        assert(std::string(to_string(ScriptBackendKind::Lua)) == "lua");
        assert(std::string(to_string(ScriptBackendKind::Null)) == "null");
    }

    std::puts("script_vm: all asserts passed");
    return 0;
}