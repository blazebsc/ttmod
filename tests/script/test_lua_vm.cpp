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
#include "ttmod/mod_lifecycle.hpp"
#include "ttmod/plugin_bind.hpp"
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

// PR 34 probe: echo arg0 back (borrowed strings survive; reentrant-safe).
static int bind_echo_cb(void*, const ttmod_value* args, int nargs, ttmod_value* ret, char*, size_t) {
    if (nargs < 1 || args[0].type != TT_STRING || !args[0].s) return 1;
    ret->type = TT_STRING;
    ret->s = args[0].s; // borrowed: the wrapper copies it out immediately
    return 0;
}

static int bind_fail_cb(void*, const ttmod_value*, int, ttmod_value*, char* err, size_t err_len) {
    snprintf(err, err_len, "bind boom");
    return 2;
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

    // ---- per-mod environments (roadmap PR 17) --------------------------
    // One VM, one _ENV table per mod: writes stay in the mod's table,
    // reads fall through to the shared globals (ttmod.* services visible,
    // sandbox inherited). Nil env keeps the legacy shared-globals run.
    EnvRef envA, envB;
    {
        auto a = vm->create_env("mod.a");
        assert(a.ok() && a.value().valid());
        envA = a.value();
        auto b = vm->create_env("mod.b");
        assert(b.ok() && b.value().valid());
        envB = b.value();
        assert(envA.slot != envB.slot);
        auto a2 = vm->create_env("mod.a");
        assert(a2.ok() && a2.value().slot == envA.slot); // idempotent
        assert(!vm->create_env("").ok());                // empty mod id rejected
    }
    // Unknown mod id fails before running anything (no half-run globals).
    {
        auto r = vm->run_chunk("sneaky = 1 return 1", "sneak.lua", Value::string("mod.ghost"));
        assert(!r.ok());
        auto g = vm->run_chunk("return sneaky", "read.lua", Value::nil());
        assert(g.ok() && g.value().is_nil());
    }
    // Wrong env kind fails too.
    assert(!vm->run_chunk("return 1", "n.lua", Value::number(7)).ok());
    // Isolation: A writes, B and globals read nil.
    {
        auto r = vm->run_chunk("mod_only = 42 return mod_only", "w.lua", Value::string("mod.a"));
        assert(r.ok() && r.value().as_number() == 42.0);
        auto b = vm->run_chunk("return mod_only", "r.lua", Value::string("mod.b"));
        assert(b.ok() && b.value().is_nil());
        auto g = vm->run_chunk("return mod_only", "r.lua", Value::nil());
        assert(g.ok() && g.value().is_nil());
    }
    // Sharing direction: globals (and ttmod.*) are visible through __index.
    {
        auto g = vm->run_chunk("shared_flag = 'yes' return 1", "s.lua", Value::nil());
        assert(g.ok());
        auto r = vm->run_chunk("return shared_flag", "r.lua", Value::string("mod.a"));
        assert(r.ok() && r.value().as_string() == "yes");
        g_log_calls = 0;
        auto l = vm->run_chunk("ttmod.log.info('from env') return 1", "l.lua", Value::string("mod.a"));
        assert(l.ok() && g_log_calls == 1 && g_last_message == "from env");
        auto e = vm->run_chunk("return ttmod.test.echo(6)", "e.lua", Value::string("mod.b"));
        assert(e.ok() && e.value().as_number() == 6.0);
    }
    // Sandbox inherited: load stays nil inside an env, and the env's own
    // metatable is locked against the casual getmetatable hole.
    {
        auto r = vm->run_chunk("return load == nil", "s.lua", Value::string("mod.a"));
        assert(r.ok() && r.value().as_bool(false));
        auto m = vm->run_chunk("return getmetatable(_ENV) == false", "m.lua",
                               Value::string("mod.a"));
        assert(m.ok() && m.value().as_bool(false));
    }
    // Release ends it: the id is unknown again, double release is safe.
    {
        vm->release_env(envA);
        assert(!vm->run_chunk("return 1", "x.lua", Value::string("mod.a")).ok());
        vm->release_env(envA);   // no-op, must not crash
        vm->release_env(EnvRef{}); // invalid ref, must not crash
        // B still works after A is gone.
        auto r = vm->run_chunk("return 7", "s.lua", Value::string("mod.b"));
        assert(r.ok() && r.value().as_number() == 7.0);
    }
    // ---- mod entrypoint runner (PR 30) ---------------------------------
    // Statuses, stages, and per-mod isolation through the public runner.
    {
        std::vector<ModScript> items = {
            {"mod.a", Runtime::Lua, "a.lua", "mod_a_var = 10 return 1"},
            {"mod.b", Runtime::Lua, "b.lua", "return mod_a_var"},
            {"mod.bad", Runtime::Lua, "bad.lua", "this is not lua"},
            {"mod.boom", Runtime::Lua, "boom.lua", "error('kaput')"},
            {"mod.empty", Runtime::Lua, "empty.lua", ""},
            {"mod.future", Runtime::Luau, "f.luau", "return 1"},
        };
        auto statuses = run_mod_entrypoints(*vm, items);
        assert(statuses.size() == 6);
        assert(statuses[0].state == ModRunState::Running); // mod.a ran
        assert(statuses[1].state == ModRunState::Running); // mod.b ran...
        assert(statuses[1].id == "mod.b");
        // ...but in its own env: mod.a's globals are invisible to it.
        auto chk = vm->run_chunk("return mod_a_var", "chk.lua", Value::string("mod.b"));
        assert(chk.ok() && chk.value().is_nil());
        assert(statuses[2].state == ModRunState::Failed); // syntax
        assert(statuses[3].state == ModRunState::Failed); // runtime error
        assert(!statuses[3].last_error_line().empty());   // menu one-liner ready
        assert(statuses[4].state == ModRunState::Failed); // empty source
        assert(statuses[5].state == ModRunState::Failed); // no Luau backend yet
        // A failed mod never recovers on rerun... rerun is a fresh status
        // (the runner is stateless; Failed sticks to its own record).
        for (auto& s : statuses)
            if (s.state == ModRunState::Failed) assert(s.errors == 1);
    }

    // Memory cap: a tiny budget + an allocating loop trips before the
    // instruction budget; VM survives. Approximate by design (checked at
    // hook cadence), so assert only the message, not exact consumption.
    {
        LuaVmOptions opt;
        opt.instruction_budget = 0;   // memory-only cadence
        opt.memory_budget_mb = 1;
        // Same registry as the main VM: make_lua_vm BINDS the process-global
        // trampoline singleton to the registry it is given; a second registry
        // would dangle after this block (production has exactly one, the
        // owner's, which outlives every VM).
        auto cap = make_lua_vm(api, nullptr, std::span<const BindingDesc>(kBindings), opt);
        assert(cap.ok());
        std::unique_ptr<ScriptVm> cap_vm(std::move(cap).value());
        auto r = cap_vm->run_chunk(
            "local t = {} while true do t[#t + 1] = string.rep('x', 4096) end",
            "bomb.lua", Value::nil());
        assert(!r.ok());
        assert(r.error().message.find("memory budget") != std::string::npos);
        assert(cap_vm->run_chunk("return 1", "after.lua", Value::nil()).ok());
    }

    // ---- capability filtering -------------------------------------------
    // A gated binding fails for a mod that did not declare the permission
    // and works for one that did; outside an entrypoint run (no context)
    // everything is permitted (framework trust).
    {
        // Gate via a binding registered on the main api: mods.read.
        auto tok = api.register_fn(ApiOwner{}, ApiKey{"gate", "needread"},
                                   [](CallCtx&) -> Result<Value> {
                                       if (!caller_permitted(Permission::ModsRead))
                                           return Result<Value>::fail(Error{
                                               "gate", "needread", errcat::kType, "mods.read not granted"});
                                       return Result<Value>::ok(Value::number(1));
                                   },
                                   (uint32_t)kApiVersion, 0, 0);
        assert(tok.ok());
        assert(vm->install_api(tok.value(), ApiKey{"gate", "needread"}, (uint32_t)kApiVersion).ok());
        // Mod without the permission: Init failure with the refusal.
        {
            auto st = run_mod_entrypoints(*vm, {{"mod.nogate", Runtime::Lua, "g.lua",
                                                 "return ttmod.gate.needread()", {}}});
            assert(st.size() == 1 && st[0].state == ModRunState::Failed);
            assert(st[0].last_error_line().find("mods.read not granted") != std::string::npos);
        }
        // Mod declaring mods.read: runs.
        {
            auto st = run_mod_entrypoints(*vm, {{"mod.gated", Runtime::Lua, "g.lua",
                                                 "return ttmod.gate.needread()",
                                                 {Permission::ModsRead}}});
            assert(st.size() == 1 && st[0].state == ModRunState::Running);
        }
        // No context (plain run_chunk): trusted.
        auto tr = vm->run_chunk("return ttmod.gate.needread()", "trust.lua", Value::nil());
        assert(tr.ok() && tr.value().as_number() == 1.0);
    }

    // ---- callback lifecycle --------------------------------------------
    // retain/call/release is the only way a script function reaches the host,
    // and releasing twice is safe.
    {
        assert(!vm->call(CallbackRef{999}, nullptr, 0).ok()); // never retained
        vm->release(CallbackRef{1});                          // no-op, must not crash
    }

    // ---- native plugin bindings (PR 34) --------------------------------
    // A plugin C callback registered through the shared registry lands in
    // Lua under the forced native/<id> namespace; errors surface as Lua
    // errors with the VM left usable; arity is enforced before the
    // callback runs; duplicates fail, never replace.
    {
        ApiOwner owner{ApiOwnerKind::Plugin, "testmod"};
        auto tok = api.register_fn(owner, ApiKey{"hello", "ping"}, make_plugin_binding(bind_echo_cb, nullptr),
                                   (uint32_t)kApiVersion, 1, 1);
        assert(tok.ok());
        auto ins = vm->install_api(tok.value(), ApiKey{owner.effective_ns(), "ping"}, (uint32_t)kApiVersion);
        assert(ins.ok());
        auto r = vm->run_chunk("return ttmod.native[\"testmod\"].ping(\"hi\")", "bind.lua", Value::nil());
        assert(r.ok() && r.value().as_string() == "hi");
        auto w = vm->run_chunk("return ttmod.native[\"testmod\"].ping()", "bind0.lua", Value::nil());
        assert(!w.ok()); // arity enforced before the callback
        auto dup = api.register_fn(owner, ApiKey{"hello", "ping"}, make_plugin_binding(bind_echo_cb, nullptr),
                                   (uint32_t)kApiVersion, 1, 1);
        assert(!dup.ok());
        auto tok2 = api.register_fn(owner, ApiKey{"hello", "fail"}, make_plugin_binding(bind_fail_cb, nullptr),
                                    (uint32_t)kApiVersion, 0, 0);
        assert(tok2.ok());
        assert(vm->install_api(tok2.value(), ApiKey{owner.effective_ns(), "fail"}, (uint32_t)kApiVersion).ok());
        auto f = vm->run_chunk("return ttmod.native[\"testmod\"].fail()", "bindf.lua", Value::nil());
        assert(!f.ok());
        assert(vm->run_chunk("return 1", "after.lua", Value::nil()).ok()); // still usable
    }

    std::puts("lua_backend: all asserts passed");
    return 0;
}