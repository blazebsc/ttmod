#pragma once
// Native script bindings (roadmap PR 34): the C-to-C++ seam that lets a
// plugin's C callback live in the C++ ScriptApiRegistry.
//
// Value crossing is deliberately narrow: nil/bool/number/string cross by
// value; arrays, objects, handles, and functions arrive as TT_UNSUPPORTED
// (mirrors Value's no-Function rule). Args borrow (valid during the call
// only); ret strings are copied out immediately, so plugin stack buffers
// are fine. A throwing plugin callback is caught here and reported as a
// structured error - it must never cross the C boundary as a C++
// exception (same rule the registry applies to every NativeFn).
#include <exception>
#include <string>
#include <vector>

#include "ttmod/plugin_api.h"
#include "ttmod/script_api.hpp"
#include "ttmod/script_value.hpp"

namespace ttmod {

inline ttmod_value to_c_value(const Value& v) noexcept {
    ttmod_value out{TT_NIL, 0, 0.0, nullptr};
    switch (v.kind()) {
        case Value::Kind::Nil:
            out.type = TT_NIL;
            break;
        case Value::Kind::Bool:
            out.type = TT_BOOL;
            out.b = v.as_bool(false) ? 1 : 0;
            break;
        case Value::Kind::Number:
            out.type = TT_NUMBER;
            out.n = v.as_number(0.0);
            break;
        case Value::Kind::String:
            out.type = TT_STRING;
            out.s = v.as_string().c_str(); // borrowed: valid while v lives (the call)
            break;
        default:
            out.type = TT_UNSUPPORTED;
            break;
    }
    return out;
}

inline Value from_c_value(const ttmod_value& v) {
    switch (v.type) {
        case TT_NIL:
            return Value::nil();
        case TT_BOOL:
            return Value::boolean(v.b != 0);
        case TT_NUMBER:
            return Value::number(v.n);
        case TT_STRING:
            return Value::string(v.s ? v.s : "");
        default:
            return Value::unsupported("cbind");
    }
}

// Wrap a plugin C callback as a registry NativeFn. ctx is opaque and
// passed straight through; the wrapper owns nothing.
inline NativeFn make_plugin_binding(ttmod_native_cb cb, void* ctx) {
    return [cb, ctx](CallCtx& c) -> Result<Value> {
        if (!cb)
            return Result<Value>::fail(Error{"plugin-bind", "", errcat::kType, "null callback"});
        std::vector<ttmod_value> args;
        for (size_t i = 0; i < c.nargs; ++i) {
            const Value* v = c.arg(i);
            args.push_back(to_c_value(v ? *v : Value::nil()));
        }
        ttmod_value ret{TT_NIL, 0, 0.0, nullptr};
        char err[256] = {};
        int rc = -1;
        try {
            rc = cb(ctx, args.data(), (int)args.size(), &ret, err, sizeof err);
        } catch (...) {
            return Result<Value>::fail(Error{"plugin-bind", "", errcat::kIO, "plugin callback threw"});
        }
        if (rc != 0) {
            err[sizeof err - 1] = '\0';
            return Result<Value>::fail(
                Error{"plugin-bind", "", errcat::kIO, err[0] ? err : "plugin callback failed"});
        }
        return Result<Value>::ok(from_c_value(ret));
    };
}

} // namespace ttmod
