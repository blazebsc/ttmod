// The Luau backend: a TTMod-owned Luau VM that actually runs mods.
//
// Mirrors core/script/lua/lua_vm.cpp function-for-function; every
// divergence is commented at its site (and listed in
// third_party/luau/README.ttmod.md). The test suite asserts identical
// observable behavior across both backends.
#include "script/luau/luau_vm.hpp"

#include <cstring>
#include <memory>
#include <utility>

#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

namespace ttmod {
namespace {

class LuauVm final : public ScriptVm {
  public:
    LuauVm(ScriptApiRegistry& api, const LuauVmOptions& opt) : api_(&api), opt_(opt) {}
    ~LuauVm() override {
        if (L_) {
            // This is OUR state: closing it is correct and required (doc §71).
            // The game's states are never touched - see game_lua.cpp, which
            // only observes them.
            lua_close(L_);
        }
    }

    Result<void> create() {
        L_ = luaL_newstate();
        if (!L_) return Result<void>::fail(Error{"luau-vm", "", errcat::kIO, "cannot create Luau state"});
        // Same five libraries as the Lua backend (no coroutine/os/debug/io:
        // a mod script has no business with them inside a game process).
        // DIVERGENCE: no luaL_requiref in Luau; each luaopen_* leaves its
        // table on top, popped to keep a known depth.
        luaopen_base(L_);
        lua_pop(L_, 1);
        luaopen_table(L_);
        lua_pop(L_, 1);
        luaopen_string(L_);
        lua_pop(L_, 1);
        luaopen_math(L_);
        lua_pop(L_, 1);
        luaopen_utf8(L_);
        lua_pop(L_, 1);
        // DIVERGENCE: Luau's base has no load/dofile/loadstring (nothing to
        // deny there); getfenv/setfenv are denied instead - they are the
        // env-escape adjacent names, absent from Lua 5.4's base entirely.
        static const char* const kDenied[] = {"getfenv", "setfenv", "collectgarbage"};
        for (const char* name : kDenied) {
            lua_pushnil(L_);
            lua_setglobal(L_, name);
        }
        budget_ = opt_.instruction_budget;
        if (budget_ > 0) {
            lua_callbacks(L_)->userdata = this;
            lua_callbacks(L_)->interrupt = &LuauVm::interrupt;
        }
        return Result<void>::success();
    }

    const char* backend() const noexcept override {
        return "luau";
    }

    size_t bytes_used() const noexcept override {
        if (!L_) return 0;
        return (size_t)lua_gc(L_, LUA_GCCOUNT, 0) * 1024;
    }

    Result<Value> run_chunk(std::string_view source, std::string_view chunk_name, const Value& env) override {
        if (!L_) return Result<Value>::fail(Error{"run-chunk", "", errcat::kIO, "no vm"});
        std::string name(chunk_name);
        std::string code(source);
        // Same contract as the Lua backend: empty source is a resolution
        // bug, never a silent success.
        if (code.empty()) return Result<Value>::fail(Error{"run-chunk", name, errcat::kMissing, "empty chunk"});
        reset_budget();
        // DIVERGENCE: no luaL_loadbufferx in Luau. Compile source to
        // bytecode (Luau::compile never throws; failures encode into the
        // blob for luau_load to decode), then load it - with the env table
        // on top when the caller named a mod (luau_load takes a stack
        // index, 0 = current globals).
        std::string bytecode = Luau::compile(code);
        int envidx = 0;
        if (env.kind() == Value::Kind::String) {
            auto it = env_by_mod_.find(env.as_string());
            if (it == env_by_mod_.end())
                return Result<Value>::fail(Error{"run-chunk", name, errcat::kMissing,
                                                 "no such env: " + env.as_string()});
            lua_rawgeti(L_, LUA_REGISTRYINDEX, env_refs_[it->second]); // [env]
            envidx = lua_gettop(L_);
        } else if (env.kind() != Value::Kind::Nil) {
            return Result<Value>::fail(
                Error{"run-chunk", name, errcat::kType, "env must be nil or a mod id"});
        }
        if (luau_load(L_, ("@" + name).c_str(), bytecode.data(), bytecode.size(), envidx) != LUA_OK)
            return Result<Value>::fail(lua_error_to_result(name)); // [env?, err]
        if (envidx != 0) lua_remove(L_, -2);                       // [chunk]
        // One result value is captured and returned. Doc §31 allows this when
        // the operation's contract says so; entrypoints ignore it, and it is
        // what makes marshaling testable end to end.
        if (lua_pcall(L_, 0, 1, 0) != LUA_OK) return Result<Value>::fail(lua_error_to_result(name));
        Value out = pop_value();
        return Result<Value>::ok(std::move(out));
    }

    Result<EnvRef> create_env(std::string_view mod_id) override {
        if (!L_) return Result<EnvRef>::fail(Error{"create-env", "", errcat::kIO, "no vm"});
        if (mod_id.empty())
            return Result<EnvRef>::fail(Error{"create-env", "", errcat::kType, "empty mod id"});
        std::string id(mod_id);
        auto found = env_by_mod_.find(id);
        if (found != env_by_mod_.end()) return Result<EnvRef>::ok(EnvRef{found->second}); // idempotent
        // Same shape as the Lua backend: writes land here, reads fall
        // through to the shared globals. DIVERGENCE: lua_pushglobaltable
        // does not exist; lua_pushvalue(LUA_GLOBALSINDEX) is identical.
        lua_newtable(L_);                                        // [env]
        lua_newtable(L_);                                        // [env, mt]
        lua_pushvalue(L_, LUA_GLOBALSINDEX);                     // [env, mt, globals]
        lua_setfield(L_, -2, "__index");                         // [env, mt]
        lua_pushboolean(L_, 0);
        lua_setfield(L_, -2, "__metatable");                     // [env, mt]
        lua_setmetatable(L_, -2);                                // [env]
        int ref = lua_ref(L_, -1);
        lua_pop(L_, 1);
        uint32_t slot = next_env_++;
        env_refs_[slot] = ref;
        env_mods_[slot] = id;
        env_by_mod_[id] = slot;
        return Result<EnvRef>::ok(EnvRef{slot});
    }

    void release_env(EnvRef r) override {
        if (!L_ || !r.valid()) return;
        auto it = env_refs_.find(r.slot);
        if (it == env_refs_.end()) return;
        lua_unref(L_, it->second);
        env_refs_.erase(it);
        auto nm = env_mods_.find(r.slot);
        if (nm != env_mods_.end()) {
            env_by_mod_.erase(nm->second);
            env_mods_.erase(nm);
        }
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
        // DIVERGENCE: lua_ref does not pop (luaL_ref does); pop explicitly.
        lua_pushvalue(L_, -1);
        int ref = lua_ref(L_, -1);
        lua_pop(L_, 2);
        callbacks_[next_callback_] = ref;
        return Result<CallbackRef>::ok(CallbackRef{next_callback_++});
    }

    void release(CallbackRef r) override {
        if (!L_ || !r.valid()) return;
        auto it = callbacks_.find(r.slot);
        if (it == callbacks_.end()) return;
        lua_unref(L_, it->second);
        callbacks_.erase(it);
    }

    Result<Value> call(CallbackRef r, const Value* args, size_t nargs) override {
        if (!L_) return Result<Value>::fail(Error{"call", "", errcat::kIO, "no vm"});
        auto it = callbacks_.find(r.slot);
        if (it == callbacks_.end())
            return Result<Value>::fail(Error{"call", "", errcat::kMissing, "unknown callback"});
        reset_budget();
        // DIVERGENCE: lua_getref macro (rawgeti on the registry).
        lua_getref(L_, it->second);
        for (size_t i = 0; i < nargs; ++i) {
            if (!push_value(args[i])) return Result<Value>::fail(lua_error_to_result("call"));
        }
        if (lua_pcall(L_, (int)nargs, 1, 0) != LUA_OK) return Result<Value>::fail(lua_error_to_result("call"));
        Value out = pop_value();
        return Result<Value>::ok(std::move(out));
    }

    Result<void> install_api(uint32_t token, const ApiKey& key, uint32_t api_version) override {
        if (!L_) return Result<void>::fail(Error{"install-api", key.dotted(), errcat::kIO, "no vm"});
        (void)api_version;
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
        // DIVERGENCE: Luau names C closures (debugname for tracebacks).
        lua_pushcclosure(L_, &LuauVm::trampoline, "ttmod_trampoline", 1);
        // lua_setfield (not settable): it takes the table by index and the key
        // by name, so the closure pushed as the value is not confused with the
        // key. lua_settable would need [table, key, value] on top.
        lua_setfield(L_, -2, key.name.c_str());
        lua_pop(L_, 1); // ns
        return Result<void>::success();
    }

  private:
    // DIVERGENCE: no hook mask in Luau. The interrupt fires at safepoints
    // (loop back-edges, call/ret, gc) instead of every N instructions, so
    // the budget is an approximate step cap: a `while true do end` trips
    // immediately, while straight-line code between safepoints is not
    // counted. Same option name, same error, documented approximation -
    // and the same default that makes it generous in practice.
    static void interrupt(lua_State* L, int /*gc*/) {
        auto* self = (LuauVm*)lua_callbacks(L)->userdata;
        if (self && --self->budget_left_ <= 0) {
            lua_pushstring(L, "ttmod: script exceeded its instruction budget");
            lua_error(L);
        }
    }

    // Raises the ttmod.* entry point from inside the VM. Always safe to call
    // from Lua (a mod uses pcall); never safe to call across a TTMod Result
    // boundary.
    static int ttmod_error(lua_State* L, std::string_view message) {
        lua_pushfstring(L, "ttmod: %s", std::string(message).c_str());
        // DIVERGENCE: Luau's lua_error returns void (l_noret), not int.
        lua_error(L);
        return 0; // unreachable; satisfies the int return
    }

    // Collects arguments, dispatches, and pushes the result. Returns 0 when the
    // VM stack holds the return value, or -1 with the reason copied into `out`.
    //
    // It is SEPARATE from trampoline() on purpose. lua_error longjmps, which
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
        Result<Value> r = bound_api_registry().invoke(token, ctx.args, ctx.nargs, nullptr);
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
        std::string m = msg ? msg : "unknown Luau error";
        lua_pop(L_, 1);
        return Error{"run-chunk", std::string(chunk), errcat::kSyntax, m};
    }

    Error push_error() const {
        const char* msg = lua_tostring(L_, -1);
        Error e{"push-value", "", errcat::kType, msg ? msg : "cannot marshal value"};
        lua_pop(L_, 1);
        return e;
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
    // DIVERGENCE: lua_pushglobaltable does not exist; lua_pushvalue on
    // LUA_GLOBALSINDEX is identical.
    void set_nested(const char* path) {
        lua_pushvalue(L_, LUA_GLOBALSINDEX);
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

    // Re-arm the interrupt counter. Cheap: two stores, no syscall.
    void reset_budget() {
        budget_left_ = budget_;
        // (Re)install: create() already set it when budget_ > 0; the pointer
        // survives across calls on the same state, only the counter resets.
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
    LuauVmOptions opt_;
    lua_State* L_ = nullptr;
    int budget_ = 0;
    int budget_left_ = 0;
    std::map<uint32_t, int> callbacks_;
    uint32_t next_callback_ = 1;
    // Per-mod environments (roadmap PR 17): slot -> ref of the mod's _ENV
    // table, plus both lookup directions. Same shape as callbacks_.
    std::map<uint32_t, int> env_refs_;
    std::map<uint32_t, std::string> env_mods_;
    std::map<std::string, uint32_t> env_by_mod_;
    uint32_t next_env_ = 1;
};

// ---- Value <-> Luau stack ----------------------------------------------
// One function per Value::Kind, so the mapping is exhaustive by construction
// and a new Kind cannot be half-supported. Luau integers and vectors read
// back as numbers (same observable type a mod author reasons about).

bool LuauVm::push_value_impl(lua_State* L, const Value& v) {
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
            lua_rawseti(L, -2, (int)(i + 1)); // Lua is 1-based
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
        lua_pushinteger(L, (int)v.handle().slot);
        lua_setfield(L, -2, "slot");
        lua_pushinteger(L, (int)v.handle().generation);
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

Value LuauVm::pop_value_at_impl(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
    case LUA_TNIL:
    case LUA_TNONE:
        return Value::nil();
    case LUA_TBOOLEAN:
        return Value::boolean(lua_toboolean(L, idx) != 0);
    case LUA_TNUMBER:
        return Value::number(lua_tonumber(L, idx));
    case LUA_TSTRING: {
        size_t n = 0;
        const char* s = lua_tolstring(L, idx, &n);
        return Value::string(std::string(s ? s : "", n));
    }
    case LUA_TTABLE: {
        // ABSOLUTE index: everything below pushes values (lua_next needs a
        // key on top), so a relative -1 would resolve to the pushed nil and
        // segfault inside the table walk.
        int t = lua_absindex(L, idx);
        // Array-like when it is a pure sequence, object-like otherwise.
        // DIVERGENCE: no lua_len; lua_objlen is identical for this use.
        int len = lua_objlen(L, t);
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
            for (int i = 1; i <= len; ++i) {
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

Result<std::unique_ptr<ScriptVm>> make_luau_vm(ScriptApiRegistry& api, ScriptHost* host,
                                               std::span<const BindingDesc> table, const LuauVmOptions& opt) {
    bind_api_registry(&api);
    auto vm = std::make_unique<LuauVm>(api, opt);
    auto created = vm->create();
    if (!created.ok()) return Result<std::unique_ptr<ScriptVm>>::fail(created.error());
    if (!table.empty()) {
        auto inst = install_bindings(*vm, api, host, table);
        if (!inst.ok()) return Result<std::unique_ptr<ScriptVm>>::fail(inst.error());
    }
    return Result<std::unique_ptr<ScriptVm>>::ok(std::move(vm));
}

} // namespace ttmod
