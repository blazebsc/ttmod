#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/plugin_bind.hpp"

namespace {
// Echo callback: returns arg0 as-is (borrowed strings must survive the
// round trip through the wrapper's immediate copy-out).
int echo_cb(void*, const ttmod_value* args, int nargs, ttmod_value* ret, char*, size_t) {
    if (nargs < 1) return 1;
    *ret = args[0];
    return 0;
}
int fail_cb(void*, const ttmod_value*, int, ttmod_value*, char* err, size_t err_len) {
    snprintf(err, err_len, "nope-%d", 7);
    return 3;
}
int silent_fail_cb(void*, const ttmod_value*, int, ttmod_value*, char*, size_t) {
    return 1; // no message: wrapper supplies the generic
}
int throw_cb(void*, const ttmod_value*, int, ttmod_value*, char*, size_t) {
    throw 42; // must never cross: wrapper reports structured
}
} // namespace

int main() {
    using ttmod::Value;
    // to_c_value: scalars cross, everything else is Unsupported. Strings
    // borrow (pointer identity with the Value's storage).
    {
        std::string s = "hi";
        Value vs = Value::string(s);
        ttmod_value c = ttmod::to_c_value(vs);
        assert(c.type == TT_STRING && c.s == vs.as_string().c_str());
        assert(ttmod::to_c_value(Value::nil()).type == TT_NIL);
        assert(ttmod::to_c_value(Value::boolean(true)).b == 1);
        assert(ttmod::to_c_value(Value::number(2.5)).n == 2.5);
        assert(ttmod::to_c_value(Value::unsupported("x")).type == TT_UNSUPPORTED);
    }
    // from_c_value: round trip + null-string + unknown-type defense.
    {
        assert(ttmod::from_c_value({TT_NIL, 0, 0.0, nullptr}).is_nil());
        assert(ttmod::from_c_value({TT_BOOL, 1, 0.0, nullptr}).as_bool(false));
        assert(ttmod::from_c_value({TT_NUMBER, 0, 1.5, nullptr}).as_number(0.0) == 1.5);
        assert(ttmod::from_c_value({TT_STRING, 0, 0.0, "s"}).as_string() == "s");
        assert(ttmod::from_c_value({TT_STRING, 0, 0.0, nullptr}).as_string().empty());
        assert(ttmod::from_c_value({(ttmod_vtype)99, 0, 0.0, nullptr}).kind() == Value::Kind::Unsupported);
    }
    // Wrapper: ok path echoes, failures carry messages, throws are caught,
    // null callback fails fast without calling.
    {
        auto echo = ttmod::make_plugin_binding(echo_cb, nullptr);
        ttmod::CallCtx ctx;
        Value arg = Value::string("ping");
        ctx.args = &arg;
        ctx.nargs = 1;
        auto r = echo(ctx);
        assert(r.ok() && r.value().as_string() == "ping");
        auto fail = ttmod::make_plugin_binding(fail_cb, nullptr);
        auto rf = fail(ctx);
        assert(!rf.ok() && rf.error().message == "nope-7");
        auto silent = ttmod::make_plugin_binding(silent_fail_cb, nullptr);
        assert(!silent(ctx).ok());
        auto thr = ttmod::make_plugin_binding(throw_cb, nullptr);
        auto rt = thr(ctx);
        assert(!rt.ok());
        auto nullfn = ttmod::make_plugin_binding(nullptr, nullptr);
        assert(!nullfn(ctx).ok());
    }
    printf("plugin-bind: marshal/wrapper OK\n");
    return 0;
}
