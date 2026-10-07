// The Lua backend: a TTMod-owned VM that actually runs mods.
//
// The point of these tests is that the VM is REAL (it executes Lua, marshals
// Values both ways, enforces a sandbox) and SEPARATE (nothing here touches a
// game state, and closing it closes only ours).
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "script/lua/lua_vm.hpp"
#include "ttmod/script_api.hpp"

using namespace ttmod;

static int g_log_calls = 0;
static std::string g_last_message;

static Result<Value> api_log(CallCtx& ctx) {
    ++g_log_calls;
    g_last_message = ctx.arg_str(0);
    return Result<Value>::ok(Value::nil());
}

static Result<Value> api_add(CallCtx& ctx) {
    double a = ctx.arg_num(0, 0), b = ctx.arg_num(1, 0);
    return Result<Value>::ok(Value::number(a + b));
}

static Value::Kind g_last_echo_kind = Value::Kind::Nil;

static Result<Value> api_echo(CallCtx& ctx) {
    // Round-trip probe: whatever Lua sent comes back to Lua. The kind the
    // HOST saw is recorded, because some kinds cannot survive the return trip.
    const Value* v = ctx.arg(0);
    g_last_echo_kind = v ? v->kind() : Value::Kind::Nil;
    return Result<Value>::ok(v ? *v : Value::nil());
}

static Result<Value> api_boom(CallCtx&) {
    return Result<Value>::fail(Error{"test", "", errcat::kIO, "deliberate failure"});
}

static const BindingDesc kBindings[] = {
    {"log", "info", &api_log, 0, 1},
    {"math2", "add", &api_add, 2, 2},
    {"log", "boom", &api_boom, 0, 0},
    {"test", "echo", &api_echo, 1, 1},
};

int main() {
    ScriptApiRegistry api;

    // A real TTMod-owned VM.
    auto created = make_lua_vm(api, nullptr, std::span<const BindingDesc>(kBindings));
    assert(created.ok());
    std::unique_ptr<ScriptVm> vm(std::move(created).value());
    assert(std::string(vm->backend()) == "lua");
    assert(vm->bytes_used() > 0);

    // Plain Lua executes: the VM is a working interpreter, not a stub.
    {
        auto r = vm->run_chunk("local x = 2 + 3 return x", "t.lua", Value::nil());
        assert(r.ok());
    }
    // A ttmod.* binding installed from the table is callable from script.
    {
        g_log_calls = 0;
        auto r = vm->run_chunk("ttmod.log.info('from lua')", "t.lua", Value::nil());
        assert(r.ok());
        assert(g_log_calls == 1);
        assert(g_last_message == "from lua");
    }
    {
        auto r = vm->run_chunk("return ttmod.math2.add(2, 3)", "t.lua", Value::nil());
        assert(r.ok());
    }

    // Sandbox: the modules a mod script must not reach are simply absent.
    // No error, no crash - they are nil, like any undefined global.
    for (const char* forbidden : {"os", "io", "package", "debug", "dofile", "loadfile"}) {
        auto r = vm->run_chunk(std::string("assert(") + forbidden + " == nil)", "sandbox.lua", Value::nil());
        assert(r.ok());
    }

    // Syntax errors come back structured, never as a crash and never as a
    // silently-empty script.
    {
        auto r = vm->run_chunk("this is not lua", "bad.lua", Value::nil());
        assert(!r.ok());
        assert(r.error().object == "bad.lua");
        assert(!r.error().message.empty());
    }
    // Runtime errors likewise, and the VM survives them.
    {
        auto r = vm->run_chunk("error('boom from mod')", "bad.lua", Value::nil());
        assert(!r.ok());
        assert(r.error().message.find("boom from mod") != std::string::npos);
        // Still alive and usable.
        assert(vm->run_chunk("return 1", "after.lua", Value::nil()).ok());
    }
    // A binding that fails surfaces as a script error, catchable by pcall.
    {
        auto r = vm->run_chunk("local ok, err = pcall(function() ttmod.log.boom() end)\n"
                               "assert(ok == false and err ~= nil)",
                               "err.lua", Value::nil());
        assert(r.ok());
    }

    // Runaway protection: an infinite loop must not hang the process. The
    // default VM has a real instruction budget, so this returns an error
    // rather than spinning forever.
    {
        auto r = vm->run_chunk("while true do end", "runaway.lua", Value::nil());
        assert(!r.ok());
        // And the VM is still usable afterwards.
        assert(vm->run_chunk("return 1", "after.lua", Value::nil()).ok());
    }

    // Empty chunk is a real error, not a silent success.
    assert(!vm->run_chunk("", "empty.lua", Value::nil()).ok());

    // ---- Value marshalling, both directions ----------------------------
    // Lua -> Value -> Lua. The binding sees a Value; the script sees a table.
    {
        auto r = vm->run_chunk("return ttmod.test.echo({1, 2, 3})", "arr.lua", Value::nil());
        assert(r.ok());
        assert(r.value().kind() == Value::Kind::Array);
        assert(r.value().size() == 3);
        assert(r.value().at(2).as_number() == 3.0);
    }
    {
        auto r = vm->run_chunk("return ttmod.test.echo({name='x', n=2})", "obj.lua", Value::nil());
        assert(r.ok());
        assert(r.value().kind() == Value::Kind::Object);
        assert(r.value().find("name") && r.value().find("name")->as_string() == "x");
        assert(r.value().find("n") && r.value().find("n")->as_number() == 2.0);
    }
    {
        auto r = vm->run_chunk("return ttmod.test.echo(true)", "b.lua", Value::nil());
        assert(r.ok() && r.value().kind() == Value::Kind::Bool && r.value().as_bool());
        r = vm->run_chunk("return ttmod.test.echo('s')", "s.lua", Value::nil());
        assert(r.ok() && r.value().kind() == Value::Kind::String && r.value().as_string() == "s");
        r = vm->run_chunk("return ttmod.test.echo(nil)", "n.lua", Value::nil());
        assert(r.ok() && r.value().is_nil());
    }
    // A Lua function has NO Value representation (doc §58): the HOST sees
    // Unsupported("function"), so nothing VM-shaped can leak into a mod, and
    // the script gets nil back rather than a broken handle.
    {
        auto r = vm->run_chunk("return ttmod.test.echo(function() end)", "fn.lua", Value::nil());
        assert(r.ok());
        assert(g_last_echo_kind == Value::Kind::Unsupported);
        assert(r.value().is_nil()); // not transferable, so it reads as nil
    }
    // Nested structure survives a round trip.
    {
        auto r = vm->run_chunk("return ttmod.test.echo({a = {b = {1, 2}}})", "deep.lua", Value::nil());
        assert(r.ok());
        const Value* b = r.value().find("a") ? r.value().find("a")->find("b") : nullptr;
        assert(b && b->kind() == Value::Kind::Array && b->at(1).as_number() == 2.0);
    }

    // ---- callback lifecycle --------------------------------------------
    // retain/call/release is the only way a script function reaches the host,
    // and releasing twice is safe.
    {
        assert(!vm->call(CallbackRef{999}, nullptr, 0).ok()); // never retained
        vm->release(CallbackRef{1});                          // no-op, must not crash
    }

    std::puts("lua_backend: all asserts passed");
    return 0;
}