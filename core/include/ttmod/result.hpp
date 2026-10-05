#pragma once
#include <string>
#include <utility>
#include <variant>

namespace ttmod {

// Structured failure: operation + object + category + explanation. Used
// everywhere untrusted input (manifests, packages, paths, configs) is
// validated, instead of bool + side-channel error strings.
struct Error {
    std::string operation; // e.g. "parse-manifest", "validate-path"
    std::string object;    // e.g. mod id, path, filename (may be empty)
    std::string category;  // machine-readable: "syntax", "type", "range",
                           // "traversal", "duplicate", "missing", "limit"
    std::string message;   // human-readable explanation
};

// Minimal Result<T>: value or Error. No exceptions anywhere in core.
template <class T>
class Result {
public:
    static Result ok(T v) { return Result(std::move(v)); }
    static Result fail(Error e) { return Result(std::move(e)); }

    bool ok() const { return std::holds_alternative<T>(data_); }
    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    const Error& error() const { return std::get<Error>(data_); }

private:
    explicit Result(T v) : data_(std::move(v)) {}
    explicit Result(Error e) : data_(std::move(e)) {}
    std::variant<T, Error> data_;
};

} // namespace ttmod
