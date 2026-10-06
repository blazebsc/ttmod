#pragma once
#include <functional>
#include <string>
#include <string_view>
#include "ttmod/result.hpp"

namespace ttmod {

// Strongly typed validated mod ID. The only way to obtain a valid ModId
// is parse(), which enforces the canonical policy (see validate.hpp):
// once a ModId exists inside the validated model (manifest, discovery,
// graph), holders must NOT re-validate it.
//
// Default construction yields an empty placeholder (valid() == false) for
// display-only slots such as CLI "?" entries. It must never be used as a
// real identity: parse() is the single entry point for untrusted text.
class ModId {
  public:
    ModId() = default;
    static Result<ModId> parse(std::string_view id);

    [[nodiscard]] const std::string& str() const noexcept {
        return id_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return !id_.empty();
    }
    [[nodiscard]] bool operator==(const ModId& o) const noexcept {
        return id_ == o.id_;
    }
    [[nodiscard]] bool operator!=(const ModId& o) const noexcept {
        return id_ != o.id_;
    }
    [[nodiscard]] bool operator<(const ModId& o) const noexcept {
        return id_ < o.id_;
    }

  private:
    explicit ModId(std::string s) : id_(std::move(s)) {}
    std::string id_;
};

} // namespace ttmod

template <> struct std::hash<ttmod::ModId> {
    size_t operator()(const ttmod::ModId& id) const noexcept {
        return std::hash<std::string>{}(id.str());
    }
};
