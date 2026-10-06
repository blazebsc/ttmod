// ScriptVm: the backend contract, the Null backend (which doubles as the
// disabled runtime and as the test double), and the binding installer.
#include "ttmod/script_vm.hpp"

#include <algorithm>
#include <map>
#include <mutex>

namespace ttmod {

const char* to_string(ScriptBackendKind k) {
    switch (k) {
    case ScriptBackendKind::Lua:
        return "lua";
    default:
        return "null";
    }
}

namespace {

// The Null VM implements the contract faithfully and executes nothing. It is
// not a stub: run_chunk validates its arguments and returns a structured
// "no backend" error, which is exactly what RuntimeMode::Disabled should do.
// One implementation, two jobs - there is no separate fake to keep correct.
class NullVm final : public ScriptVm {
  public:
    explicit NullVm(ScriptApiRegistry& api) : api_(&api) {}

    const char* backend() const noexcept override {
        return "null";
    }
    size_t bytes_used() const noexcept override {
        return 0;
    }

    Result<Value> run_chunk(std::string_view source, std::string_view chunk_name, const Value& env) override {
        if (source.empty())
            return Result<Value>::fail(Error{"run-chunk", std::string(chunk_name), errcat::kMissing, "empty chunk"});
        return Result<Value>::fail(
            Error{"run-chunk", std::string(chunk_name), errcat::kIO, "no script backend (null runtime)"});
    }

    Result<CallbackRef> retain(const Value& fn) override {
        if (fn.kind() != Value::Kind::Unsupported) {
            return Result<CallbackRef>::fail(
                Error{"retain-callback", "", errcat::kType, "null backend has no callbacks"});
        }
        return Result<CallbackRef>::fail(Error{"retain-callback", "", errcat::kIO, "no script backend (null runtime)"});
    }
    void release(CallbackRef) override {}
    Result<Value> call(CallbackRef ref, const Value* args, size_t nargs) override {
        return Result<Value>::fail(Error{"call-callback", "", errcat::kIO, "no script backend (null runtime)"});
    }

    Result<void> install_api(uint32_t slot, const ApiKey& key, uint32_t api_version) override {
        return Result<void>::success();
    }

  private:
    ScriptApiRegistry* api_;
};

} // namespace

std::unique_ptr<ScriptVm> make_null_vm(ScriptApiRegistry& api) {
    return std::make_unique<NullVm>(api);
}

Result<void> install_bindings(ScriptVm& vm, ScriptApiRegistry& api, ScriptHost* host,
                              std::span<const BindingDesc> table) {
    const ApiOwner owner{}; // Framework
    size_t installed = 0;
    for (const BindingDesc& d : table) {
        if (!d.ns || !d.name) continue;
        ApiKey key;
        key.ns = d.ns;
        key.name = d.name;
        if (!key.valid())
            return Result<void>::fail(Error{"install-bindings", key.dotted(), errcat::kType, "invalid key"});
        // The binding captures the host, so a ttmod.* function can reach the
        // dispatcher, the scheduler and mod config without a global.
        NativeFn fn = d.fn;
        if (host) {
            fn = [fn, host](CallCtx& ctx) {
                ctx.host = host;
                return fn(ctx);
            };
        }
        auto tok = api.register_fn(owner, key, fn, (uint32_t)kApiVersion, d.min_args, d.max_args);
        if (!tok.ok()) return Result<void>::fail(tok.error());
        auto ins = vm.install_api(tok.value(), key, (uint32_t)kApiVersion);
        if (!ins.ok()) return ins;
        ++installed;
    }
    return Result<void>::success();
}

} // namespace ttmod