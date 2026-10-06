// Lua backend for ScriptVm. The only file that knows what a lua_State is.
#include "lua_vm.hpp"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <cstdio>
#include <cstring>
#include <memory>

namespace ttmod {
namespace {

// One light userdata per registry token. The VM stores the token in the
// closure's upvalue and dispatches through bound_api_registry(): no plugin
// function pointer is ever stored in the VM, so a revoked owner cannot leave a
// dangling call behind.
constexpr const char* kTokenUpvalue = "ttmod_token";

ScriptApiRegistry& registry() {
    return bound_api_registry();
}

class LuaVm final : public ScriptVm {
  public:
    LuaVm(ScriptApiRegistry& api, const LuaVmOptions& opt) : api_(&api), opt_(opt) {}
    ~LuaVm() override {
        if (L_) {
            // This is OUR state: closing it is correct and required (doc §71).
            // The game's states are never touched - see game_lua.cpp, which
            // only observes them.
            lua_close(L_);
        }
    }

    Result<void> create() {
        L_ = luaL_newstate();
        if (!L_) return Result<void>::fail(Error{"lua-vm", "", errcat::kIO, "cannot create Lua state"});
        // The standard library, minus the modules a mod script has no business
        // using inside a game process. TTMod does its own module loading, so
        // package/require would be both redundant and an escape hatch.
        static const luaL_Reg kLibs[] = {{LUA_GNAME, luaopen_base},
                                         {LUA_TABLIBNAME, luaopen_table},
                                         {LUA_STRLIBNAME, luaopen_string},
                                         {LUA_MATHLIBNAME, luaopen_math},
                                         {LUA_UTF8LIBNAME, luaopen_utf8}};
        // Each requiref leaves the module on the stack; drop them all so the
        // VM starts every call from a known depth.
        for (const auto& lib : kLibs) {
            luaL_requiref(L_, lib.name, lib.func, 1);
            lua_pop(L_, 1);
        }
        // os/io/package/debug are NOT opened (see lua_vm.hpp). The remaining
        // file/code entry points live in the BASE library, so they have to be
        // removed explicitly: inside a game process a mod script has no
        // business loading files or compiling strings.
        static const char* const kDenied[] = {"dofile", "loadfile", "load", "collectgarbage"};
        for (const char* name : kDenied) {
            lua_pushnil(L_);
            lua_setglobal(L_, name);
        }
        budget_ = opt_.instruction_budget;
        if (budget_ > 0) {
            lua_sethook(L_, &LuaVm::hook, LUA_MASKCOUNT, budget_);
        }
        return Result<void>::success();
    }

    const char* backend() const noexcept override {
        return "lua";
    }

    size_t bytes_used() const noexcept override {
        if (!L_) return 0;
        return (size_t)lua_gc(L_, LUA_GCCOUNT) * 1024;
    }

    Result<Value> run_chunk(std::string_view source, std::string_view chunk_name, const Value& env) override {
        if (!L_) return Result<Value>::fail(Error{"run-chunk", "", errcat::kIO, "no vm"});
        std::string name(chunk_name);
        std::string code(source);
        // Lua would happily accept an empty chunk; the contract does not. An
        // empty source means the entrypoint was resolved wrongly, and silently
        // "succeeding" is how a mod ends up doing nothing.
        if (code.empty()) return Result<Value>::fail(Error{"run-chunk", name, errcat::kMissing, "empty chunk"});
        reset_budget();
        if (luaL_loadbufferx(L_, code.data(), code.size(), ("@" + name).c_str(), "t") != LUA_OK) {
            return Result<Value>::fail(lua_error_to_result(name));
        }
        // Shared-VM decision (doc §9): every mod runs in the SAME globals table,
        // so ttmod.mods.get() is a table lookup rather than a cross-VM call.
        // Isolation is pcall + disarm, not separate heaps (doc §63).
        push_env(env);
        // One result value is captured and returned. Doc §31 allows this when
        // the operation's contract says so; entrypoints ignore it, and it is
        // what makes marshaling testable end to end.
        if (lua_pcall(L_, 0, 1, 0) != LUA_OK) return Result<Value>::fail(lua_error_to_result(name));
        Value out = pop_value();
        return Result<Value>::ok(std::move(out));
    }

    Result<CallbackRef> retain(const Value& fn) override {
        if (!L_) return Result<CallbackRef>::fail(Error{"retain", "", errcat::kIO, "no vm"});
        reset_budget();
        if (!push_value(fn)) return Result<CallbackRef>::fail(push_error());
        if (!lua_isfunction(L_, -1)) {
            lua_pop(L_, 1);
            return Result<CallbackRef>::fail(Error{"retain", "", errcat::kType, "value is not a function"});
        }
        // Anchoring in the registry keeps the closure alive across calls, so a
        // host-held CallbackRef never points at a collected stack slot.
        lua_pushvalue(L_, -1);
        int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
        callbacks_[next_callback_] = ref;
        lua_pop(L_, 1);
        return Result<CallbackRef>::ok(CallbackRef{next_callback_++});
    }

    void release(CallbackRef r) override {
        if (!L_ || !r.valid()) return;
        auto it = callbacks_.find(r.slot);
        if (it == callbacks_.end()) return;
        luaL_unref(L_, LUA_REGISTRYINDEX, it->second);
        callbacks_.erase(it);
    }

    Result<Value> call(CallbackRef r, const Value* args, size_t nargs) override {
        if (!L_) return Result<Value>::fail(Error{"call", "", errcat::kIO, "no vm"});
        auto it = callbacks_.find(r.slot);
        if (it == callbacks_.end())
            return Result<Value>::fail(Error{"call", "", errcat::kMissing, "callback already released"});
        reset_budget();
        lua_rawgeti(L_, LUA_REGISTRYINDEX, it->second);
        if (!lua_isfunction(L_, -1)) {
            lua_pop(L_, 1);
            return Result<Value>::fail(Error{"call", "", errcat::kType, "callback is not a function"});
        }
        for (size_t i = 0; i < nargs; ++i) {
            if (!push_value(args[i])) return Result<Value>::fail(lua_error_to_result("call"));
        }
        if (lua_pcall(L_, (int)nargs, 1, 0) != LUA_OK) return Result<Value>::fail(lua_error_to_result("call"));
        Value out = pop_value();
        return Result<Value>::ok(std::move(out));
    }

    Result<void> install_api(uint32_t token, const ApiKey& key, uint32_t api_version) override {
        if (!L_) return Result<void>::fail(Error{"install-api", key.dotted(), errcat::kIO, "no vm"});
        set_nested(key.ns.c_str());
        if (lua_getfield(L_, -1, key.name.c_str()) == LUA_TFUNCTION) {
            lua_pop(L_, 2);
            return Result<void>::success(); // already installed (idempotent)
        }
        lua_pop(L_, 1);
        // Upvalues FIRST: lua_pushcclosure consumes the top N values, so
        // pushing the key before them would have the closure swallow the key
        // and leave [table, closure] instead of [table, key, closure].
        lua_pushlightuserdata(L_, (void*)(uintptr_t)token);
        lua_pushcclosure(L_, &LuaVm::trampoline, 1);
        // lua_setfield (not settable): it takes the table by index and the key
        // by name, so the closure pushed as the value is not confused with the
        // key. lua_settable would need [table, key, value] on top.
        lua_setfield(L_, -2, key.name.c_str());
        lua_pop(L_, 1); // ns
        return Result<void>::success();
    }

  private:
    static void hook(lua_State* L, lua_Debug*) {
        // Budget exhausted: error out of the running chunk instead of letting
        // a runaway script hang the script thread. doc §62: the scheduler's
        // cascade bound is not preemption; THIS is.
        luaL_error(L, "ttmod: script exceeded its instruction budget");
    }

    // Raises the ttmod.* entry point from inside the VM. Always safe to call
    // from Lua (a mod uses pcall); never safe to call across a TTMod Result
    // boundary.
    static int ttmod_error(lua_State* L, std::string_view message) {
        lua_pushfstring(L, "ttmod: %s", std::string(message).c_str());
        return lua_error(L);
    }

    // Collects arguments, dispatches, and pushes the result. Returns 0 when the
    // VM stack holds the return value, or -1 with the reason copied into `out`.
    //
    // It is SEPARATE from trampoline() on purpose. luaL_error longjmps, which
    // skips C++ destructors; if it fired while `args` (a vector<Value>) or the
    // Result holding an Error were in scope, every error from every binding
    // would leak. So this function RETURNS, its locals are destroyed normally,
    // and only then does the caller raise - and the caller owns nothing but a
    // char array and an int, which a longjmp cannot leak.
    static int dispatch(lua_State* L, uint32_t token, char* out, size_t out_len) {
        auto fail = [&](std::string_view why) {
            size_t n = why.size() < out_len - 1 ? why.size() : out_len - 1;
            memcpy(out, why.data(), n);
            out[n] = '\0';
            return -1;
        };

        int nargs = lua_gettop(L);
        std::vector<Value> args;
        args.reserve((size_t)nargs);
        for (int i = 1; i <= nargs; ++i) args.push_back(pop_value_at_impl(L, i));

        CallCtx ctx;
        ctx.args = args.data();
        ctx.nargs = args.size();
        Result<Value> r = registry().invoke(token, ctx.args, ctx.nargs, nullptr);
        if (!r.ok()) {
            // Copy out and let the Result die here, before the caller longjmps.
            return fail(r.error().message);
        }
        if (!push_value_impl(L, r.value())) return fail("cannot marshal result");
        return 0;
    }

    static int trampoline(lua_State* L) {
        auto* ud = lua_touserdata(L, lua_upvalueindex(1));
        uint32_t token = (uint32_t)(uintptr_t)ud;
        // Trivially destructible locals only: this frame is allowed to longjmp.
        char msg[256] = {0};
        if (dispatch(L, token, msg, sizeof msg) < 0) return ttmod_error(L, msg);
        return 1;
    }

    Error lua_error_to_result(std::string_view chunk) {
        const char* msg = lua_tostring(L_, -1);
        std::string m = msg ? msg : "unknown Lua error";
        lua_pop(L_, 1);
        return Error{"run-chunk", std::string(chunk), errcat::kSyntax, m};
    }

    Error push_error() const {
        const char* msg = lua_tostring(L_, -1);
        Error e{"push-value", "", errcat::kType, msg ? msg : "cannot marshal value"};
        lua_pop(L_, 1);
        return e;
    }

    void push_env(const Value& env) {
        // One shared globals table: the shared-VM decision (doc §9). Nothing is
        // pushed here - the loaded chunk is already on top and pcall must call
        // THAT. A future per-mod _ENV sets it via lua_setupvalue, it does not
        // go on the stack.
        (void)env;
    }

    // Navigates/creates the table chain under the single `ttmod` root, leaving
    // the innermost table on top for the caller to add the leaf to.
    //
    // Everything a mod sees lives under `ttmod.*` (doc §13): mods cannot collide
    // with the game's own globals, and the namespace is obviously
    // framework-owned. "native/graphics" creates two levels for plugin
    // registrations.
    // Leaves the table addressed by `path` on top, creating it if needed.
    // Everything lives under one `ttmod` root (doc §13) so mods cannot collide
    // with the game's own globals.
    //
    // Stack discipline (Lua's settable consumes value AND key):
    //   [parent, new] -> dup new -> setfield(-3) -> [parent, new] -> remove
    // The duplicate is what survives the assignment, so the new table ends up
    // on top; lua_setfield resolves its index before pushing the key, hence -3.
    void set_nested(const char* path) {
        lua_pushglobaltable(L_);
        if (lua_getfield(L_, -1, "ttmod") == LUA_TTABLE) {
            lua_remove(L_, -2); // keep the table found, drop globals
        } else {
            lua_pop(L_, 1);   // drop the nil
            lua_newtable(L_); // [globals, new]
            lua_pushvalue(L_, -1);
            // lua_setfield resolves its table index BEFORE pushing the key
            // internally, so with [globals, new, new] the parent is -3 and the
            // extra copy is what survives the assignment.
            lua_setfield(L_, -3, "ttmod");
            lua_remove(L_, -2); // [new]
        }
        std::string p(path);
        size_t i = 0;
        while (i < p.size()) {
            size_t j = p.find('/', i);
            std::string seg = p.substr(i, j == std::string::npos ? j : j - i);
            if (!seg.empty()) {
                if (lua_getfield(L_, -1, seg.c_str()) == LUA_TTABLE) {
                    lua_remove(L_, -2); // descend into the existing table
                } else {
                    lua_pop(L_, 1);
                    lua_newtable(L_); // [parent, new]
                    lua_pushvalue(L_, -1);
                    lua_setfield(L_, -3, seg.c_str()); // parent.seg = new
                    lua_remove(L_, -2);                // [new]
                }
            }
            if (j == std::string::npos) break;
            i = j + 1;
        }
    }

    // Re-arm the instruction counter. Cheap: lua_sethook with the same mask.
    void reset_budget() {
        if (budget_ > 0) lua_sethook(L_, &LuaVm::hook, LUA_MASKCOUNT, budget_);
    }

    Value pop_value_at(int idx) {
        return pop_value_at_impl(L_, idx);
    }
    bool push_value(const Value& v) {
        return push_value_impl(L_, v);
    }
    Value pop_value() {
        return pop_value_at_impl(L_, -1);
    }

    static Value pop_value_at_impl(lua_State* L, int idx);
    static bool push_value_impl(lua_State* L, const Value& v);

    ScriptApiRegistry* api_;
    LuaVmOptions opt_;
    lua_State* L_ = nullptr;
    int budget_ = 0;
    std::map<uint32_t, int> callbacks_;
    uint32_t next_callback_ = 1;
};

// ---- Value <-> Lua stack ------------------------------------------------
// One function per Value::Kind, so the mapping is exhaustive by construction
// and a new Kind cannot be half-supported.

bool LuaVm::push_value_impl(lua_State* L, const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Nil:
        lua_pushnil(L);
        return true;
    case Value::Kind::Bool:
        lua_pushboolean(L, v.as_bool());
        return true;
    case Value::Kind::Number:
        lua_pushnumber(L, v.as_number());
        return true;
    case Value::Kind::String:
        lua_pushlstring(L, v.as_string().data(), v.as_string().size());
        return true;
    case Value::Kind::Array: {
        lua_createtable(L, (int)v.size(), 0);
        for (size_t i = 0; i < v.size(); ++i) {
            if (!push_value_impl(L, v.at(i))) return false;
            lua_rawseti(L, -2, (lua_Integer)(i + 1)); // Lua is 1-based
        }
        return true;
    }
    case Value::Kind::Object: {
        lua_createtable(L, 0, (int)v.size());
        for (auto& [k, e] : v.fields()) {
            if (!push_value_impl(L, e)) return false;
            lua_setfield(L, -2, k.c_str());
        }
        return true;
    }
    case Value::Kind::GameObject: {
        // Never the raw game pointer (doc §§58, 59): an opaque table with
        // the handle's numbers, so a script cannot forge or inspect one.
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)v.handle().slot);
        lua_setfield(L, -2, "slot");
        lua_pushinteger(L, (lua_Integer)v.handle().generation);
        lua_setfield(L, -2, "generation");
        return true;
    }
    case Value::Kind::Unsupported: {
        // Pushed as plain nil: a call returns ONE value, so pushing the
        // reason too would make the next pop read the reason instead of
        // the result. The reason stays host-side, where a binding sees it
        // through CallCtx; the script sees "nothing", never a lie.
        lua_pushnil(L);
        return true;
    }
    }
    return false;
}

Value LuaVm::pop_value_at_impl(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
    case LUA_TNIL:
    case LUA_TNONE:
        return Value::nil();
    case LUA_TBOOLEAN:
        return Value::boolean(lua_toboolean(L, idx) != 0);
    case LUA_TNUMBER:
        return Value::number((double)lua_tonumber(L, idx));
    case LUA_TSTRING: {
        size_t n = 0;
        const char* s = lua_tolstring(L, idx, &n);
        return Value::string(std::string(s ? s : "", n));
    }
    case LUA_TTABLE: {
        // ABSOLUTE index: everything below pushes values (lua_next needs a
        // key on top), so a relative -1 would resolve to the pushed nil and
        // segfault inside luaH_next.
        int t = lua_absindex(L, idx);
        // Array-like when it is a pure sequence, object-like otherwise.
        lua_len(L, t);
        lua_Integer len = lua_tointeger(L, -1);
        lua_pop(L, 1);
        // lua_next needs the key on top; it consumes it and pushes the
        // next key/value pair. Popping the value and leaving the key is
        // what makes the walk legal - popping both is the classic
        // "invalid key to 'next'" bug.
        bool seq = true;
        lua_pushnil(L);
        while (lua_next(L, t) != 0) {
            if (lua_type(L, -2) != LUA_TNUMBER) {
                seq = false;
                lua_pop(L, 1); // value
                lua_pop(L, 1); // key, and stop walking
                break;
            }
            lua_pop(L, 1); // value only; key stays for the next lua_next
        }
        if (seq && len >= 0) {
            auto items = std::make_shared<Value::ArrayVec>();
            items->reserve((size_t)len);
            for (lua_Integer i = 1; i <= len; ++i) {
                lua_rawgeti(L, t, i);
                items->push_back(pop_value_at_impl(L, -1));
                lua_pop(L, 1);
            }
            return Value::array(items);
        }
        auto fields = std::make_shared<Value::Fields>();
        lua_pushnil(L);
        while (lua_next(L, t) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                size_t n = 0;
                const char* k = lua_tolstring(L, -2, &n);
                fields->emplace_back(std::string(k ? k : "", n), pop_value_at_impl(L, -1));
            }
            lua_pop(L, 1); // value; key stays for the next lua_next
        }
        return Value::object(fields);
    }
    case LUA_TFUNCTION:
        // Functions have no Value representation on purpose (doc §58).
        // Crossing happens via retain()/call(), never by value.
        return Value::unsupported("function");
    default:
        return Value::unsupported(lua_typename(L, t));
    }
}

} // namespace

Result<std::unique_ptr<ScriptVm>> make_lua_vm(ScriptApiRegistry& api, ScriptHost* host,
                                              std::span<const BindingDesc> table, const LuaVmOptions& opt) {
    bind_api_registry(&api);
    auto vm = std::make_unique<LuaVm>(api, opt);
    auto created = vm->create();
    if (!created.ok()) return Result<std::unique_ptr<ScriptVm>>::fail(created.error());
    if (!table.empty()) {
        auto inst = install_bindings(*vm, api, host, table);
        if (!inst.ok()) return Result<std::unique_ptr<ScriptVm>>::fail(inst.error());
    }
    return Result<std::unique_ptr<ScriptVm>>::ok(std::move(vm));
}

} // namespace ttmod