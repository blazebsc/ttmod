#include "ttmod/version.hpp"

namespace ttmod {

Result<Version> Version::parse(std::string_view s) {
    auto fail = [&](const std::string& msg) {
        return Result<Version>::fail(Error{"parse-version", std::string(s), errcat::kSyntax, msg});
    };
    if (s.size() > kMaxVersionLen) return fail("version too long");
    Version v;
    v.raw_ = std::string(s);
    std::string_view core = s;
    // Prerelease is ignored for gating (documented), but must still be
    // well-formed so garbage cannot masquerade as a version.
    size_t dash = s.find('-');
    if (dash != std::string_view::npos) {
        core = s.substr(0, dash);
        std::string_view pre = s.substr(dash + 1);
        if (pre.empty() || pre.size() > kMaxPrereleaseLen) return fail("bad prerelease");
        for (char c : pre) {
            bool ok =
                (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
            if (!ok) return fail("bad prerelease");
        }
    }
    if (core.empty()) {
        // Only the empty string is the zero version 0.0.0: manifests may omit
        // "version" entirely and that must compare, not fail. Anything with a
        // dash and no core ("-1.0", "--") is malformed.
        if (dash != std::string_view::npos) return fail("empty core");
        v.count_ = 0;
        return Result<Version>::ok(std::move(v));
    }
    uint8_t n = 0;
    size_t i = 0;
    while (true) {
        if (n >= kMaxVersionParts) return fail("too many parts");
        size_t j = core.find('.', i);
        std::string_view part = core.substr(i, j == std::string_view::npos ? j : j - i);
        if (part.empty()) return fail("empty part");
        if (part.size() > 9) return fail("part too long");
        uint64_t val = 0;
        for (char c : part) {
            if (c < '0' || c > '9') return fail("non-numeric part");
            val = val * 10 + (uint64_t)(c - '0');
        }
        if (val > kMaxVersionPart) return fail("part overflow");
        v.parts_[n++] = val;
        if (j == std::string_view::npos) break;
        i = j + 1;
    }
    v.count_ = n;
    return Result<Version>::ok(std::move(v));
}

int Version::compare(const Version& o) const {
    size_t n = count_ > o.count_ ? count_ : o.count_;
    for (size_t k = 0; k < n; ++k) {
        uint64_t a = k < count_ ? parts_[k] : 0;
        uint64_t b = k < o.count_ ? o.parts_[k] : 0;
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

Result<VersionConstraint> VersionConstraint::parse(std::string_view spec) {
    VersionConstraint c;
    c.raw_ = std::string(spec);
    if (spec.empty()) return Result<VersionConstraint>::ok(std::move(c)); // matches anything
    bool has_operator = false;
    if (spec.starts_with(">=")) {
        c.op_ = Op::Ge;
        spec.remove_prefix(2);
        has_operator = true;
    } else if (spec.starts_with("<=")) {
        c.op_ = Op::Le;
        spec.remove_prefix(2);
        has_operator = true;
    } else if (spec.starts_with("=")) {
        c.op_ = Op::Eq;
        spec.remove_prefix(1);
        has_operator = true;
    } else if (spec.starts_with(">")) {
        c.op_ = Op::Gt;
        spec.remove_prefix(1);
        has_operator = true;
    } else if (spec.starts_with("<")) {
        c.op_ = Op::Lt;
        spec.remove_prefix(1);
        has_operator = true;
    }
    if (has_operator && spec.empty()) {
        return Result<VersionConstraint>::fail(
            Error{"parse-version-constraint", c.raw_, errcat::kSyntax, "missing version after operator"});
    }
    auto base = Version::parse(spec);
    if (!base.ok())
        return Result<VersionConstraint>::fail(
            Error{"parse-version-constraint", c.raw_, base.error().category, base.error().message});
    c.base_ = base.value();
    c.empty_ = false;
    return Result<VersionConstraint>::ok(std::move(c));
}

bool VersionConstraint::satisfied_by(const Version& v) const {
    if (empty_) return true;
    int c = v.compare(base_);
    switch (op_) {
    case Op::Ge:
        return c >= 0;
    case Op::Le:
        return c <= 0;
    case Op::Eq:
        return c == 0;
    case Op::Gt:
        return c > 0;
    case Op::Lt:
        return c < 0;
    }
    return false;
}

} // namespace ttmod