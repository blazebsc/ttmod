#include "ttmod/script_api.hpp"

#include <exception>

namespace ttmod {

bool ApiKey::valid() const noexcept {
    if (ns.empty() || name.empty()) return false;
    // Dots and slashes are structural, so a segment may not contain them.
    // Without this, "a.b.c" would parse into an ambiguous key.
    for (char c : ns)
        if (c == '.' || c == '/' || c == ' ') return false;
    for (char c : name)
        if (c == '.' || c == '/' || c == ' ') return false;
    return true;
}

std::string ApiKey::dotted() const {
    return ns + "." + name;
}

Result<ApiKey> parse_api_key(std::string_view dotted) {
    auto fail = [&](const std::string& msg) {
        return Result<ApiKey>::fail(Error{"parse-api-key", std::string(dotted), errcat::kType, msg});
    };
    size_t dot = dotted.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= dotted.size()) return fail("expected ns.name");
    ApiKey k;
    k.ns = std::string(dotted.substr(0, dot));
    k.name = std::string(dotted.substr(dot + 1));
    if (!k.valid()) return fail("invalid segment");
    return Result<ApiKey>::ok(std::move(k));
}

const Value* CallCtx::arg(size_t i) const noexcept {
    if (!args || i >= nargs) return nullptr;
    return &args[i];
}

std::string CallCtx::arg_str(size_t i) const {
    const Value* v = arg(i);
    return v ? v->as_string() : std::string();
}

double CallCtx::arg_num(size_t i, double def) const {
    const Value* v = arg(i);
    return v ? v->as_number(def) : def;
}

Result<uint32_t> ScriptApiRegistry::register_fn(const ApiOwner& owner, ApiKey key, NativeFn fn, uint32_t since_api,
                                                int min_args, int max_args) {
    if (!key.valid()) return Result<uint32_t>::fail(Error{"register-api", key.dotted(), errcat::kType, "invalid key"});
    if (!fn) return Result<uint32_t>::fail(Error{"register-api", key.dotted(), errcat::kType, "null function"});
    if (since_api > (uint32_t)kApiVersion)
        return Result<uint32_t>::fail(
            Error{"register-api", key.dotted(), errcat::kRange, "requires a newer framework"});

    // Namespacing happens HERE, not at the call site: a plugin physically
    // cannot overwrite ttmod.game or another plugin's namespace.
    std::string ns = owner.effective_ns();
    if (!ns.empty() && key.ns != ns) key.ns = ns;

    std::lock_guard<std::mutex> lock(mtx_);
    if (by_key_.count(key))
        return Result<uint32_t>::fail(Error{"register-api", key.dotted(), errcat::kDuplicate, "already registered"});
    if (min_args < 0 || (max_args >= 0 && max_args < min_args))
        return Result<uint32_t>::fail(Error{"register-api", key.dotted(), errcat::kRange, "bad arity"});

    ApiEntry e;
    e.key = key;
    e.owner = owner;
    e.since_api = since_api;
    e.min_args = min_args;
    e.max_args = max_args;
    e.fn = std::move(fn);
    uint32_t token = next_token_++;
    entries_[token] = std::move(e);
    by_key_[key] = token;
    return Result<uint32_t>::ok(token);
}

void ScriptApiRegistry::unregister(uint32_t token) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = entries_.find(token);
    if (it == entries_.end()) return;
    // Tombstone, not erase: the VM's installed stub keeps this token, and every
    // later call becomes a structured "missing" instead of a jump into freed
    // memory.
    by_key_.erase(it->second.key);
    it->second.revoked = true;
    it->second.fn = nullptr;
}

void ScriptApiRegistry::unregister_owner(const ApiOwner& owner) {
    std::vector<uint32_t> tokens;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& [token, e] : entries_)
            if (!e.revoked && e.owner == owner) tokens.push_back(token);
    }
    for (uint32_t t : tokens) unregister(t);
}

const ApiEntry* ScriptApiRegistry::find(const ApiKey& k) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = by_key_.find(k);
    if (it == by_key_.end()) return nullptr;
    return &entries_.at(it->second);
}

std::vector<ApiKey> ScriptApiRegistry::keys() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<ApiKey> out;
    out.reserve(by_key_.size());
    for (auto& [k, _] : by_key_) out.push_back(k);
    return out; // std::map => already sorted, so this is deterministic
}

Result<Value> ScriptApiRegistry::invoke(uint32_t token, const Value* args, size_t nargs, const ApiOwner* caller) const {
    ApiEntry entry;
    uint32_t api_version = 0;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = entries_.find(token);
        if (it == entries_.end()) return Result<Value>::fail(Error{"invoke-api", "", errcat::kMissing, "no such api"});
        if (it->second.revoked)
            return Result<Value>::fail(Error{"invoke-api", it->second.key.dotted(), errcat::kMissing, "api revoked"});
        // Arity is checked before the call: a plugin that reads past nargs
        // would read whatever the stack happened to hold.
        if ((int)nargs < it->second.min_args || (it->second.max_args >= 0 && (int)nargs > it->second.max_args)) {
            return Result<Value>::fail(Error{"invoke-api", it->second.key.dotted(), errcat::kRange, "bad arity"});
        }
        entry = it->second; // copy the std::function; do not call under the lock
        api_version = (uint32_t)kApiVersion;
    }

    CallCtx ctx;
    ctx.args = args;
    ctx.nargs = nargs;
    ctx.api_version = api_version;
    ctx.caller = caller;
    // A plugin NativeFn that unwinds is a plugin bug. Core has no exceptions,
    // so it is contained here at the boundary rather than crossing it.
    try {
        return entry.fn(ctx);
    } catch (const std::exception& e) {
        return Result<Value>::fail(
            Error{"invoke-api", entry.key.dotted(), errcat::kIO, std::string("plugin threw: ") + e.what()});
    } catch (...) {
        return Result<Value>::fail(Error{"invoke-api", entry.key.dotted(), errcat::kIO, "plugin threw"});
    }
}

size_t ScriptApiRegistry::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return by_key_.size();
}

namespace {
std::mutex g_bind_mtx;
ScriptApiRegistry* g_bound = nullptr;
} // namespace

ScriptApiRegistry& bound_api_registry() {
    std::lock_guard<std::mutex> lock(g_bind_mtx);
    if (!g_bound) g_bound = new ScriptApiRegistry(); // process-lifetime, by design
    return *g_bound;
}

void bind_api_registry(ScriptApiRegistry* r) {
    std::lock_guard<std::mutex> lock(g_bind_mtx);
    g_bound = r;
}

} // namespace ttmod