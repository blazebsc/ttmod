#include "ttmod/script_value.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace ttmod {
namespace {

const std::string& empty_string() {
    static const std::string kEmpty;
    return kEmpty;
}

const Value::ArrayVec& empty_array() {
    static const Value::ArrayVec kEmpty;
    return kEmpty;
}

const Value::Fields& empty_fields() {
    static const Value::Fields kEmpty;
    return kEmpty;
}

// Limits are process-wide and set once at startup. A mutex rather than a
// plain global because tests mutate them; the read path is one atomic load in
// practice and this is never a hot path.
Value::Limits g_limits;
std::mutex g_limits_mtx;

} // namespace

// Recursive clamp. Over-limit subtrees become Unsupported(why) IN PLACE, so
// the surrounding value still crosses and the mod sees exactly what dropped.
Value Value::clamp(Value v, size_t depth, size_t& nodes, const Limits& lim) {
    using Kind = Value::Kind;
    if (++nodes > lim.nodes) return Value::unsupported("too many nodes");
    if (depth > lim.depth) return Value::unsupported("too deep");
    switch (v.kind()) {
    case Kind::String:
        if (v.as_string().size() > lim.string_bytes) return Value::unsupported("string too long");
        return v;
    case Kind::Array: {
        auto items = std::make_shared<ArrayVec>();
        items->reserve(v.items().size());
        for (auto& e : v.items()) items->push_back(clamp(e, depth + 1, nodes, lim));
        Value out;
        out.kind_ = Kind::Array;
        out.arr_ = items;
        return out;
    }
    case Kind::Object: {
        auto f = std::make_shared<Fields>();
        f->reserve(v.fields().size());
        for (auto& [k, e] : v.fields()) f->emplace_back(k, clamp(e, depth + 1, nodes, lim));
        Value out;
        out.kind_ = Kind::Object;
        out.obj_ = f;
        return out;
    }
    default:
        return v;
    }
}

namespace {
// Clamp at construction time, using the process limits.
Value clamped(Value v) {
    Value::Limits lim = Value::limits();
    size_t nodes = 0;
    return Value::clamp(std::move(v), 0, nodes, lim);
}
} // namespace

Value Value::boolean(bool v) {
    Value x;
    x.kind_ = Kind::Bool;
    x.b_ = v;
    return x;
}

Value Value::number(double v) {
    Value x;
    x.kind_ = Kind::Number;
    x.num_ = v;
    return x;
}

Value Value::string(std::string v) {
    Value x;
    if (v.size() > limits().string_bytes) {
        x.kind_ = Kind::Unsupported;
        x.str_ = std::make_shared<const std::string>("string too long");
        return x;
    }
    x.kind_ = Kind::String;
    x.str_ = std::make_shared<const std::string>(std::move(v));
    return clamped(std::move(x));
}

Value Value::array(std::shared_ptr<const ArrayVec> v) {
    Value x;
    if (!v) {
        x.kind_ = Kind::Array;
        x.arr_ = std::make_shared<const ArrayVec>();
        return x;
    }
    if (v->size() > limits().array_items) {
        x.kind_ = Kind::Unsupported;
        x.str_ = std::make_shared<const std::string>("array too long");
        return x;
    }
    x.kind_ = Kind::Array;
    x.arr_ = std::move(v);
    return clamped(std::move(x));
}

Value Value::object(std::shared_ptr<const Fields> f) {
    Value x;
    if (!f) {
        x.kind_ = Kind::Object;
        x.obj_ = std::make_shared<const Fields>();
        return x;
    }
    if (f->size() > limits().object_fields) {
        x.kind_ = Kind::Unsupported;
        x.str_ = std::make_shared<const std::string>("object too large");
        return x;
    }
    x.kind_ = Kind::Object;
    // Sorted and de-duplicated at construction: marshaled values obey the same
    // determinism contract as every other externally observable output.
    auto sorted = std::make_shared<Fields>();
    sorted->reserve(f->size());
    for (auto& kv : *f) sorted->push_back(kv);
    std::sort(sorted->begin(), sorted->end(), [](const Field& a, const Field& b) { return a.first < b.first; });
    sorted->erase(
        std::unique(sorted->begin(), sorted->end(), [](const Field& a, const Field& b) { return a.first == b.first; }),
        sorted->end());
    x.obj_ = sorted;
    return clamped(std::move(x));
}

Value Value::game_object(GameObjectHandle h) {
    Value x;
    x.kind_ = Kind::GameObject;
    x.handle_ = h;
    return x;
}

Value Value::unsupported(std::string why) {
    Value x;
    x.kind_ = Kind::Unsupported;
    x.str_ = std::make_shared<const std::string>(std::move(why));
    return x;
}

bool Value::truthy() const noexcept {
    switch (kind_) {
    case Kind::Nil:
    case Kind::Unsupported:
        return false;
    case Kind::Bool:
        return b_;
    case Kind::Number:
        return num_ != 0.0;
    default:
        return true;
    }
}

bool Value::as_bool(bool def) const noexcept {
    return kind_ == Kind::Bool ? b_ : def;
}
double Value::as_number(double def) const noexcept {
    return kind_ == Kind::Number ? num_ : def;
}

const std::string& Value::as_string() const noexcept {
    return (kind_ == Kind::String || kind_ == Kind::Unsupported) && str_ ? *str_ : empty_string();
}

size_t Value::size() const noexcept {
    if (kind_ == Kind::Array) return arr_ ? arr_->size() : 0;
    if (kind_ == Kind::Object) return obj_ ? obj_->size() : 0;
    return 0;
}

const Value& Value::at(size_t i) const noexcept {
    if (kind_ == Kind::Array && arr_ && i < arr_->size()) return (*arr_)[i];
    static const Value kNil;
    return kNil;
}

const Value* Value::find(std::string_view key) const noexcept {
    if (kind_ != Kind::Object || !obj_) return nullptr;
    // Fields are sorted, so binary search.
    auto it = std::lower_bound(obj_->begin(), obj_->end(), key,
                               [](const Field& f, std::string_view k) { return f.first < k; });
    if (it == obj_->end() || it->first != key) return nullptr;
    return &it->second;
}

const std::string& Value::why() const noexcept {
    // Same storage as a string; only meaningful for Kind::Unsupported.
    return as_string();
}

const Value::ArrayVec& Value::items() const noexcept {
    return (kind_ == Kind::Array && arr_) ? *arr_ : empty_array();
}

const Value::Fields& Value::fields() const noexcept {
    return (kind_ == Kind::Object && obj_) ? *obj_ : empty_fields();
}

std::string Value::to_string() const {
    using Kind = Value::Kind;
    switch (kind_) {
    case Kind::Nil:
        return "nil";
    case Kind::Bool:
        return b_ ? "true" : "false";
    case Kind::Number: {
        if (std::isnan(num_)) return "nan";
        if (std::isinf(num_)) return num_ > 0 ? "inf" : "-inf";
        char b[40];
        snprintf(b, sizeof b, "%.17g", num_);
        return b;
    }
    case Kind::String:
        return as_string();
    case Kind::Array: {
        std::string out = "[";
        bool first = true;
        for (auto& e : items()) {
            if (!first) out += ", ";
            first = false;
            out += e.to_string();
        }
        return out + "]";
    }
    case Kind::Object: {
        std::string out = "{";
        bool first = true;
        for (auto& [k, e] : fields()) {
            if (!first) out += ", ";
            first = false;
            out += k + "=" + e.to_string();
        }
        return out + "}";
    }
    case Kind::GameObject: {
        char b[64];
        snprintf(b, sizeof b, "<object %llu/%u>", (unsigned long long)handle_.slot, handle_.generation);
        return b;
    }
    case Kind::Unsupported:
        return "<unsupported: " + why() + ">";
    }
    return "nil";
}

void Value::set_limits(Limits l) noexcept {
    std::lock_guard<std::mutex> lock(g_limits_mtx);
    g_limits = l;
}

Value::Limits Value::limits() noexcept {
    std::lock_guard<std::mutex> lock(g_limits_mtx);
    return g_limits;
}

bool value_equal(const Value& a, const Value& b) noexcept {
    using Kind = Value::Kind;
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
    case Kind::Nil:
    case Kind::Unsupported:
        return true;
    case Kind::Bool:
        return a.as_bool() == b.as_bool();
    case Kind::Number:
        return a.as_number() == b.as_number();
    case Kind::String:
        return a.as_string() == b.as_string();
    case Kind::GameObject:
        return a.handle() == b.handle();
    case Kind::Array: {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!value_equal(a.at(i), b.at(i))) return false;
        return true;
    }
    case Kind::Object: {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a.fields()[i].first != b.fields()[i].first) return false;
            else if (!value_equal(a.fields()[i].second, b.fields()[i].second)) return false;
        return true;
    }
    }
    return false;
}

size_t value_node_count(const Value& v) noexcept {
    using Kind = Value::Kind;
    size_t n = 1;
    if (v.kind() == Kind::Array) {
        for (auto& e : v.items()) n += value_node_count(e);
    } else if (v.kind() == Kind::Object) {
        for (auto& [_, e] : v.fields()) n += value_node_count(e);
    }
    return n;
}

} // namespace ttmod