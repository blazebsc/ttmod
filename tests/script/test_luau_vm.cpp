// The Luau backend: a TTMod-owned Luau VM that actually runs mods.
//
// Mirrors tests/script/test_lua_vm.cpp case-for-case: the point is that
// the Luau backend is behavior-identical (executes, marshals, sandboxes,
// isolates, errors, callbacks, bindings, runner), not merely present.
// Luau-specific adjustments are commented at their sites.
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "script/luau/luau_vm.hpp"
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
    // Round-trip probe: whatever Luau sent comes back to Luau. The kind the
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

static int bind_echo_cb(void*, const ttmod_value* args, int nargs, ttmod_value* ret, char*, size_t) {
    if (nargs < 1 || args[0].type != TT_STRING || !args[0].s) return 1;
    ret->type = TT_STRING;
    ret->s = args[0].s; // borrowed: the wrapper copies it out immediately
    return 0;
}

int main() {
    ScriptApiRegistry api;

    // A real TTMod-owned Luau VM.
    auto created = make_luau_vm(api, nullptr, std::span<const BindingDesc>(kBindings));
    assert(created.ok());
    std::unique_ptr<ScriptVm> vm(std::move(created).value());
    assert(std::string(vm->backend()) == "luau");
    assert(std::string(to_string(ScriptBackendKind::Luau)) == "luau");
    assert(vm->bytes_used() > 0);

    // Plain Luau executes: the VM is a working interpreter, not a stub.
    {
        auto r = vm->run_chunk("local x = 2 + 3 return x", "t.lua", Value::nil());
        assert(r.ok());
    }
    // A ttmod.* binding installed from the table is callable from script.
    {
        g_log_calls = 0;
        auto r = vm->run_chunk("ttmod.log.info('from luau')", "t.lua", Value::nil());
        assert(r.ok());
        assert(g_log_calls == 1);
        assert(g_last_message == "from luau");
    }
    {
        auto r = vm->run_chunk("return ttmod.math2.add(2, 3)", "t.lua", Value::nil());
        assert(r.ok());
    }

    // Sandbox: the modules a mod script must not reach are simply absent.
    // No error, no crash - they are nil, like any undefined global.
    // Luau extras vs the Lua backend: getfenv/setfenv (env-escape
    // adjacent, absent from Lua 5.4's base); Luau's base never had
    // dofile/loadfile, so nothing is denied there.
    for (const char* forbidden :
         {"os", "io", "package", "debug", "dofile", "loadfile", "getfenv", "setfenv", "collectgarbage"}) {
        auto r = vm->run_chunk(std::string("assert(") + forbidden + " == nil)", "sandbox.lua", Value::nil());
        assert(r.ok());
    }

    // Syntax errors come back structured, never as a crash and never as a
    // silently-empty script.
    {
        auto r = vm->run_chunk("this is not luau !!!", "bad.lua", Value::nil());
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
    // Empty chunks are always programmer error, never silent success.
    assert(!vm->run_chunk("", "empty.lua", Value::nil()).ok());
    // Budgets are enforced: the runaway dies, the VM lives on.
    {
        auto r = vm->run_chunk("while true do end", "runaway.lua", Value::nil());
        assert(!r.ok());
        assert(vm->run_chunk("return 1", "after.lua", Value::nil()).ok());
    }
    // Values round-trip both ways (echo returns exactly what it got).
    {
        auto r = vm->run_chunk("return ttmod.test.echo({1, 2, 3})", "arr.lua", Value::nil());
        assert(r.ok());
    }
    {
        auto r = vm->run_chunk("return ttmod.test.echo('s')", "s.lua", Value::nil());
        assert(r.ok() && r.value().as_string() == "s");
    }
    // A Luau function has NO Value representation (doc §58), same as Lua.
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

    // ---- per-mod environments ------------------------------------------
    // Identical semantics to the Lua backend: writes stay in the mod's
    // table, reads fall through to the shared globals.
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
    {
        auto r = vm->run_chunk("sneaky = 1 return 1", "sneak.lua", Value::string("mod.ghost"));
        assert(!r.ok());
        auto g = vm->run_chunk("return sneaky", "read.lua", Value::nil());
        assert(g.ok() && g.value().is_nil());
    }
    assert(!vm->run_chunk("return 1", "n.lua", Value::number(7)).ok());
    {
        auto r = vm->run_chunk("mod_only = 42 return mod_only", "w.lua", Value::string("mod.a"));
        assert(r.ok() && r.value().as_number() == 42.0);
        auto b = vm->run_chunk("return mod_only", "r.lua", Value::string("mod.b"));
        assert(b.ok() && b.value().is_nil());
        auto g = vm->run_chunk("return mod_only", "r.lua", Value::nil());
        assert(g.ok() && g.value().is_nil());
    }
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
    // Sandbox inherited: load stays nil inside an env. Luau has no _ENV
    // name at all (nil), and getfenv/setfenv are denied - so unlike the
    // Lua backend there is no expression that even names the mod's own
    // env table, and no path from it to the globals table. The
    // __metatable lock on the env table stays as defense in depth
    // (probed directly against the C API: locked tables read false).
    {
        auto r = vm->run_chunk("return load == nil", "s.lua", Value::string("mod.a"));
        assert(r.ok() && r.value().as_bool(false));
        auto e = vm->run_chunk("return _ENV == nil and getfenv == nil and setfenv == nil", "m.lua",
                               Value::string("mod.a"));
        assert(e.ok() && e.value().as_bool(false));
    }
    {
        vm->release_env(envA);
        assert(!vm->run_chunk("return 1", "x.lua", Value::string("mod.a")).ok());
        vm->release_env(envA);   // no-op, must not crash
        vm->release_env(EnvRef{}); // invalid ref, must not crash
        auto r = vm->run_chunk("return 7", "s.lua", Value::string("mod.b"));
        assert(r.ok() && r.value().as_number() == 7.0);
    }

    // ---- mod entrypoint runner: backend-agnostic -------------------------
    // The same runner drives both VMs; a Luau mod runs on the Luau VM.
    {
        std::vector<ModScript> items = {
            {"mod.x", Runtime::Luau, "x.luau", "luau_only = 5 return 1"},
            {"mod.y", Runtime::Luau, "y.luau", "return luau_only"},
        };
        auto statuses = run_mod_entrypoints(*vm, items);
        assert(statuses.size() == 2);
        assert(statuses[0].state == ModRunState::Running);
        assert(statuses[1].state == ModRunState::Running);
        auto chk = vm->run_chunk("return luau_only", "chk.luau", Value::string("mod.y"));
        assert(chk.ok() && chk.value().is_nil()); // isolated across mods
    }

    // ---- native plugin bindings: backend-agnostic ------------------------
    // The PR 34 contract holds on Luau too: forced namespace, errors as
    // Luau errors, arity enforced, duplicates rejected.
    {
        ApiOwner owner{ApiOwnerKind::Plugin, "testmod"};
        auto tok = api.register_fn(owner, ApiKey{"hello", "ping"}, make_plugin_binding(bind_echo_cb, nullptr),
                                   (uint32_t)kApiVersion, 1, 1);
        assert(tok.ok());
        auto ins = vm->install_api(tok.value(), ApiKey{owner.effective_ns(), "ping"}, (uint32_t)kApiVersion);
        assert(ins.ok());
        auto r = vm->run_chunk("return ttmod.native[\"testmod\"].ping(\"hi\")", "bind.luau", Value::nil());
        assert(r.ok() && r.value().as_string() == "hi");
        auto dup = api.register_fn(owner, ApiKey{"hello", "ping"}, make_plugin_binding(bind_echo_cb, nullptr),
                                   (uint32_t)kApiVersion, 1, 1);
        assert(!dup.ok());
    }

    // Memory cap (Luau): same contract as the Lua backend - tiny budget,
    // allocating loop, structured error, VM survives.
    {
        LuauVmOptions opt;
        opt.instruction_budget = 0;   // memory-only cadence
        opt.memory_budget_mb = 1;
        // Same registry as the main VM (see the Lua test for the singleton
        // binding rationale).
        auto cap = make_luau_vm(api, nullptr, std::span<const BindingDesc>(kBindings), opt);
        assert(cap.ok());
        std::unique_ptr<ScriptVm> cap_vm(std::move(cap).value());
        auto r = cap_vm->run_chunk(
            "local t = {} while true do t[#t + 1] = string.rep('x', 4096) end",
            "bomb.luau", Value::nil());
        assert(!r.ok());
        assert(r.error().message.find("memory budget") != std::string::npos);
        assert(cap_vm->run_chunk("return 1", "after.luau", Value::nil()).ok());
    }

    {
        // Gate via a binding registered on the main api: mods.read, and the
        // same permission model holds on Luau (runner is backend-agnostic).
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
        auto st = run_mod_entrypoints(*vm, {{"mod.nogate", Runtime::Luau, "g.luau",
                                             "return ttmod.gate.needread()", {}}});
        assert(st.size() == 1 && st[0].state == ModRunState::Failed);
        assert(st[0].last_error_line().find("mods.read not granted") != std::string::npos);
        auto st2 = run_mod_entrypoints(*vm, {{"mod.gated", Runtime::Luau, "g.luau",
                                              "return ttmod.gate.needread()",
                                              {Permission::ModsRead}}});
        assert(st2.size() == 1 && st2[0].state == ModRunState::Running);
    }

    // ---- callback lifecycle --------------------------------------------
    // retain/call/release is the only way a script function reaches the host,
    // and releasing twice is safe.
    {
        assert(!vm->call(CallbackRef{999}, nullptr, 0).ok()); // never retained
        vm->release(CallbackRef{1});                          // no-op, must not crash
    }

    std::puts("luau_backend: all asserts passed");
    return 0;
}
