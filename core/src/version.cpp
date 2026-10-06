#include "ttmod/version.hpp"

namespace ttmod {

std::vector<std::string> Version::split(std::string s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('.', i);
        if (j == std::string::npos) j = s.size();
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

static long num_prefix(const std::string& s) {
    long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        v = v * 10 + (c - '0');
    }
    return v;
}

int Version::compare(const Version& o) const {
    size_t n = parts_.size() > o.parts_.size() ? parts_.size() : o.parts_.size();
    for (size_t k = 0; k < n; ++k) {
        long a = k < parts_.size() ? num_prefix(parts_[k]) : 0;
        long b = k < o.parts_.size() ? num_prefix(o.parts_[k]) : 0;
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

VersionConstraint::VersionConstraint(std::string spec) : base_("") {
    static const char* ops[] = {">=", "<=", "=", ">", "<"};
    for (auto* o : ops) {
        std::string p(o);
        if (spec.compare(0, p.size(), p) == 0) {
            op_ = p;
            spec = spec.substr(p.size());
            break;
        }
    }
    if (op_.empty() && !spec.empty()) op_ = ">=";
    raw_ = spec;
    base_ = Version(spec);
}

bool VersionConstraint::satisfied_by(const Version& v) const {
    if (empty()) return true;
    int c = v.compare(base_);
    if (op_ == ">=") return c >= 0;
    if (op_ == "<=") return c <= 0;
    if (op_ == ">") return c > 0;
    if (op_ == "<") return c < 0;
    return c == 0; // "="
}

} // namespace ttmod
