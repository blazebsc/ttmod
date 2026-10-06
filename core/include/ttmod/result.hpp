#pragma once
#include <cstdlib>
#include <string>
#include <utility>
#include <variant>

namespace ttmod {

// Structured failure: operation + object + category + explanation. Used
// everywhere untrusted input (manifests, packages, paths, configs) is
// validated, instead of bool + side-channel error strings.
//
// Standard categories (use these constants, not ad-hoc strings):
namespace errcat {
inline constexpr const char* kSyntax = "syntax";
inline constexpr const char* kType = "type";
inline constexpr const char* kRange = "range";
inline constexpr const char* kTraversal = "traversal";
inline constexpr const char* kDuplicate = "duplicate";
inline constexpr const char* kMissing = "missing";
inline constexpr const char* kLimit = "limit";
inline constexpr const char* kIO = "io";
} // namespace errcat
struct Error {
    std::string operation; // e.g. "parse-manifest", "validate-path"
    std::string object;    // e.g. mod id, path, filename (may be empty)
    std::string category;  // machine-readable: see errcat::*
    std::string message;   // human-readable explanation
};

inline Error make_error(std::string operation, std::string object, std::string category,
                        std::string message) {
    return Error{std::move(operation), std::move(object), std::move(category), std::move(message)};
}

// Minimal Result<T>: value or Error. No exceptions anywhere in core:
// misuse (value() on error, error() on success) aborts - it is a caller
// bug, never an expected outcome. Prefer try_value()/value_or()/has_value()
// when failure is expected.
template <class T>
class Result {
public:
    static Result ok(T v) { return Result(std::move(v)); }
    static Result fail(Error e) { return Result(std::move(e)); }

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool has_value() const noexcept { return ok(); }
    // Precondition: ok(). Aborts otherwise.
    const T& value() const {
        if (const T* p = std::get_if<T>(&data_)) return *p;
        std::abort();
    }
    T& value() {
        if (T* p = std::get_if<T>(&data_)) return *p;
        std::abort();
    }
    const T* try_value() const noexcept { return std::get_if<T>(&data_); }
    T value_or(T fallback) const { return ok() ? value() : std::move(fallback); }
    // Precondition: !ok(). Aborts otherwise.
    const Error& error() const {
        if (const Error* p = std::get_if<Error>(&data_)) return *p;
        std::abort();
    }

private:
    explicit Result(T v) : data_(std::move(v)) {}
    explicit Result(Error e) : data_(std::move(e)) {}
    std::variant<T, Error> data_;
};

// Void specialization for fallible operations with no value.
// Factory is success() (not ok()) because C++ cannot overload a static
// ok() with the instance ok().
template <>
class Result<void> {
public:
    static Result success() { return Result(true, {}); }
    static Result fail(Error e) { return Result(false, std::move(e)); }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    // Precondition: !ok(). Aborts otherwise.
    const Error& error() const {
        if (!ok_) return error_;
        std::abort();
    }

private:
    Result(bool ok, Error e) : ok_(ok), error_(std::move(e)) {}
    bool ok_ = false;
    Error error_;
};

} // namespace ttmod
