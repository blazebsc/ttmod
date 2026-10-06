#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "ttmod/result.hpp"

namespace ttmod {
// Single source of framework version (M23 diagnostics, M17 API policy).
inline constexpr const char* kVersion = "0.12.0";
inline constexpr int kApiVersion = 5; // mirrors TTMOD_PLUGIN_API_VERSION

// Bounded version type (Step 3). Deliberately NOT strict SemVer: game mod
// versions are loose by nature, and existing mods ship "1.0", "2.0-beta",
// "" freely. The accepted grammar is fixed and total instead:
//
//   version    := core [ "-" prerelease ]
//   core       := part { "." part }          1..4 parts, missing = 0
//   part       := DIGIT { DIGIT }            1..9 digits, no sign, no spaces
//   prerelease := 1..32 chars, [0-9A-Za-z.-]
//   whole string: at most 64 chars
//
// Rules that the previous free-form comparison got wrong:
// - every core part must be fully numeric ("1.0.0" ok, "1.x" rejected)
// - no numeric overflow: parts > 999999999 are rejected, not wrapped
// - "1.0.0-beta" == "1.0.0" for gating (prerelease is ignored, documented)
// - "" is a VALID version equal to 0.0.0 (manifests may omit "version")
// - comparison is total and deterministic: more parts compare as 0
// Invalid input never becomes a Version; parse() returns a structured Error.
inline constexpr size_t kMaxVersionLen = 64;
inline constexpr size_t kMaxVersionParts = 4;
inline constexpr uint64_t kMaxVersionPart = 999999999ull;
inline constexpr size_t kMaxPrereleaseLen = 32;

class Version {
  public:
    // The one entry point for untrusted text. "" parses as 0.0.0.
    static Result<Version> parse(std::string_view s);

    // Dotted-numeric compare: -1/0/+1. Total order, no overflow.
    int compare(const Version& o) const;
    [[nodiscard]] const std::string& str() const noexcept {
        return raw_;
    }
    [[nodiscard]] bool operator==(const Version& o) const noexcept {
        return compare(o) == 0;
    }

  private:
    std::array<uint64_t, kMaxVersionParts> parts_{};
    uint8_t count_ = 0;
    std::string raw_; // original text, for display
};

// Dependency constraint: optional operator prefix + version.
// "" (empty) matches anything. Operators: >= (default when bare), =, >,
// <=, <. Bare "1.0" means ">= 1.0" (minimum version, the depends
// convention). Malformed specs are rejected by parse().
class VersionConstraint {
  public:
    static Result<VersionConstraint> parse(std::string_view spec);

    bool satisfied_by(const Version& v) const;
    [[nodiscard]] bool empty() const {
        return empty_;
    }
    [[nodiscard]] const std::string& str() const noexcept {
        return raw_;
    }

  private:
    bool empty_ = true;
    std::string op_ = ">=";
    Version base_; // 0.0.0 when empty_
    std::string raw_;
};

} // namespace ttmod