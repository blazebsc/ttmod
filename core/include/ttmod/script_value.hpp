#pragma once
// The cross-VM value (doc §§11, 58, 59).
//
// This is the ONLY type that may cross a scripting-VM boundary. It carries
// no code, no VM identity, and no raw pointer: a Function/Thread/Userdata
// kind is deliberately absent, so there is no representation in which a
// lua_State or a game pointer can leak into a mod script by accident.
//
// Game objects cross as an opaque, generation-checked GameObjectHandle. The
// game-side table that owns the real pointer is a loader concept and stays
// there; core stays portable and pointer-free.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ttmod/result.hpp"

namespace ttmod {

// Opaque, generation-checked reference to a game object. Generation is bumped
// when a slot is recycled, so a handle to a destroyed object fails
// validation instead of dereferencing freed memory (doc §60).
struct GameObjectHandle {
    uint64_t slot = 0; // 0 = none
    uint32_t generation = 0;

    [[nodiscard]] bool valid() const noexcept {
        return slot != 0;
    }
    [[nodiscard]] bool operator==(const GameObjectHandle& o) const noexcept {
        return slot == o.slot && generation == o.generation;
    }
};

class Value {
  public:
    enum class Kind { Nil, Bool, Number, String, Array, Object, GameObject, Unsupported };

    using Field = std::pair<std::string, Value>;
    using Fields = std::vector<Field>;
    // Composites are shared and immutable: a Value can be handed to another
    // thread or another VM with no aliasing reasoning. Scalars allocate nothing.
    using ArrayVec = std::vector<Value>;

    Value() = default;

    static Value nil() {
        return Value();
    }
    static Value boolean(bool v);
    static Value number(double v);
    static Value string(std::string v);
    static Value array(std::shared_ptr<const ArrayVec> v);
    static Value object(std::shared_ptr<const Fields> f);
    static Value game_object(GameObjectHandle h);
    // A value that could not be expressed at the boundary. NOT an error and
    // NOT nil: nil reads as "no such thing" and yields nil-deref bugs in mods.
    // Scripts see nil plus a reason, so they can tell "absent" from "denied".
    static Value unsupported(std::string why);

    [[nodiscard]] Kind kind() const noexcept {
        return kind_;
    }
    [[nodiscard]] bool is_nil() const noexcept {
        return kind_ == Kind::Nil;
    }
    // Unsupported is falsey but not nil (doc §11).
    [[nodiscard]] bool truthy() const noexcept;

    [[nodiscard]] bool as_bool(bool def = false) const noexcept;
    [[nodiscard]] double as_number(double def = 0.0) const noexcept;
    // "" for any non-string kind. Never throws, never allocates.
    [[nodiscard]] const std::string& as_string() const noexcept;

    [[nodiscard]] size_t size() const noexcept;             // array items / object fields
    [[nodiscard]] const Value& at(size_t i) const noexcept; // nil Value if out of range
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
    [[nodiscard]] const ArrayVec& items() const noexcept;
    [[nodiscard]] const Fields& fields() const noexcept;

    [[nodiscard]] GameObjectHandle handle() const noexcept {
        return handle_;
    }
    [[nodiscard]] const std::string& why() const noexcept; // Unsupported reason

    // Deterministic diagnostic rendering for logs/CLI/tests. NOT a Lua literal
    // generator: the backend owns literal syntax.
    [[nodiscard]] std::string to_string() const;

    // Limits, applied at construction, recursively. An over-limit subtree is
    // replaced IN PLACE by Unsupported(why), so the surrounding value still
    // crosses and the mod can see exactly what was dropped.
    struct Limits {
        size_t depth = 8;
        size_t nodes = 256;
        size_t array_items = 256;
        size_t object_fields = 64;
        size_t string_bytes = 64 * 1024;
    };
    static void set_limits(Limits l) noexcept;
    static Limits limits() noexcept;

    // The limit pass, as a member so it can build raw Values. It MUST NOT call
    // the public constructors: those clamp too, so each nested level would
    // reset the depth budget and recurse forever. (That bug segfaulted the
    // depth test.) Public only so the .cpp free function can reach it; treat
    // it as internal - nothing outside script_value.cpp should call it.
    static Value clamp(Value v, size_t depth, size_t& nodes, const Limits& lim);

  private:
    Kind kind_ = Kind::Nil;
    bool b_ = false;
    double num_ = 0.0;
    GameObjectHandle handle_;
    std::shared_ptr<const std::string> str_;
    std::shared_ptr<const ArrayVec> arr_;
    std::shared_ptr<const Fields> obj_;
};

// Structural equality: type-sensitive, so 1 ~= "1" and true ~= 1.
bool value_equal(const Value& a, const Value& b) noexcept;
size_t value_node_count(const Value& v) noexcept;

} // namespace ttmod