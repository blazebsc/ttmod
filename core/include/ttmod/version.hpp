#pragma once
#include <string>
#include <vector>

namespace ttmod {
// Single source of framework version (M23 diagnostics, M17 API policy).
inline constexpr const char* kVersion = "0.12.0";
inline constexpr int kApiVersion = 5; // mirrors TTMOD_PLUGIN_API_VERSION

// Deliberately simplified version scheme (Stage C): dotted numeric parts,
// missing parts are 0 ("1.2" == "1.2.0"), non-numeric tails ignored for
// gating ("1.0.0-beta" == "1.0.0"). NOT strict SemVer by decision - game
// mod versions are loose by nature.
class Version {
public:
    explicit Version(std::string s) : parts_(split(std::move(s))) {}
    int compare(const Version& o) const;

private:
    static std::vector<std::string> split(std::string s);
    std::vector<std::string> parts_;
};

// Dependency constraint: optional operator prefix + version.
// "" (empty) matches anything. Operators: >= (default when bare), =, >,
// <=, <. Bare "1.0" means ">= 1.0" (minimum version, the depends convention).
class VersionConstraint {
public:
    explicit VersionConstraint(std::string spec);
    bool satisfied_by(const Version& v) const;
    bool empty() const { return raw_.empty(); }

private:
    std::string op_;
    Version base_;
    std::string raw_;
};

} // namespace ttmod
