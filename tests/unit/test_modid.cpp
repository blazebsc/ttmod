// ModId: validated strong type. Invalid ids must not be constructible,
// valid ids must round-trip exactly, and ordering must be stable (the
// dependency graph and discovery both sort by it).
#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "ttmod/manifest.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/validate.hpp"
#include "ttmod/version.hpp"

int main() {
    using ttmod::ModId;
    using ttmod::Version;
    using ttmod::VersionConstraint;

    // Valid ids parse and round-trip byte-for-byte.
    for (const char* good : {"a", "a.b", "hello.mcsm", "pkg.test", "c.mod", "a_b-c.d", "Mod123", "x1"}) {
        auto r = ModId::parse(good);
        assert(r.ok());
        assert(r.value().valid());
        assert(r.value().str() == good);
    }
    // Boundary: exactly 64 chars is the documented maximum.
    {
        std::string max64(64, 'a');
        assert(ModId::parse(max64).ok());
        std::string max65(65, 'a');
        assert(!ModId::parse(max65).ok());
    }
    // Invalid ids are rejected by the same policy is_valid_mod_id enforces.
    for (const char* bad : {"", ".", "..", "../evil", "..\\evil", "a/b", "a\\b", "C:evil", "a b", " lead", "trail ",
                            "a..b", "a;b", "*", "a\nb"}) {
        std::string b = bad;
        assert(!ModId::parse(b).ok());
        assert(!ttmod::is_valid_mod_id(b));
    }
    std::string too_long(65, 'a');
    for (const std::string& bad :
         std::vector<std::string>{"", too_long, ".", "..", "a..b", " a", "a ", "a/b", "a\\b", "é"}) {
        auto r = ModId::parse(bad);
        assert(!r.ok() && r.error().operation == "parse-mod-id" && r.error().category == ttmod::errcat::kSyntax);
    }
    // Embedded NUL: built with an explicit length, since a const char* literal
    // would truncate to "a" and be valid.
    {
        std::string nul("a\0b", 3);
        assert(!ModId::parse(nul).ok());
    }
    // Empty and 65+ failures carry a structured Error.
    {
        auto r = ModId::parse("../evil");
        assert(!r.ok() && r.error().category == ttmod::errcat::kSyntax);
        assert(r.error().object == "../evil");
    }

    // Equality, inequality, and ordering (deterministic load order).
    auto a = ModId::parse("a.mod").value();
    auto a2 = ModId::parse("a.mod").value();
    auto b = ModId::parse("b.mod").value();
    assert(a == a2 && a != b);
    assert(a < b && !(b < a));
    assert((a < b) == (a.str() < b.str()));
    assert(!(a < a2));
    auto mixed_case = ModId::parse("Mod.A").value();
    auto lower_case = ModId::parse("mod.a").value();
    assert(mixed_case.str() == "Mod.A" && mixed_case != lower_case);
    assert(ModId::parse(mixed_case.str()).value() == mixed_case);
    assert(!ModId::parse(" a").ok()); // no trimming

    // Versions: bounded grammar, no overflow, total order.
    auto ver = [](const char* s) {
        auto r = ttmod::Version::parse(s);
        assert(r.ok());
        return r.value();
    };
    assert(ver("1.2.0").compare(ver("1.2")) == 0);
    assert(ver("1.10").compare(ver("1.9")) > 0);
    assert(ver("").compare(ver("0")) == 0);               // omitted version == 0.0.0
    assert(ver("1.0.0-beta").compare(ver("1.0.0")) == 0); // prerelease ignored for gating
    assert(Version::parse("999999999").ok());
    assert(!Version::parse("1000000000").ok());
    assert(Version::parse("1.2.3.4").ok());
    assert(!Version::parse("1.2.3.4.5").ok());
    assert(Version::parse("1.0-" + std::string(32, 'a')).ok());
    assert(!Version::parse("1.0-" + std::string(33, 'a')).ok());
    assert(ver("01.002") == ver("1.2"));
    assert(!Version::parse(" 1.0").ok());
    assert(!Version::parse("1.0 ").ok());
    const std::string max64 = "999999999.999999999.999999999.999999999-" + std::string(24, 'a');
    assert(max64.size() == 64 && Version::parse(max64).ok());
    assert(!Version::parse(max64 + "a").ok());
    // Malformed / overflowing versions never become a Version.
    for (const char* bad : {" ", "1.", ".1", "1..0", "1.x", "v1.0", "-1.0", "1.0.0.0.0", "9999999999", "1 0", "1.0.0-",
                            "1.0.0-beta!", "1.0+build", "-", "-1.0", "--"}) {
        std::string b = bad;
        assert(!ttmod::Version::parse(b).ok());
    }
    // Constraints.
    auto con = [](const char* s) {
        auto r = ttmod::VersionConstraint::parse(s);
        assert(r.ok());
        return r.value();
    };
    assert(con("").satisfied_by(ver("9.9")));
    assert(con("2.0").satisfied_by(ver("2.1.0")));
    assert(!con("3.0").satisfied_by(ver("2.1.0")));
    assert(con("=2.1.0").satisfied_by(ver("2.1.0")));
    assert(!con("=2.1.0").satisfied_by(ver("2.1.1")));
    assert(con("<3.0").satisfied_by(ver("2.9.9")));
    assert(!con(">2.0").satisfied_by(ver("2.0")));
    assert(con("<=2.0").satisfied_by(ver("2.0")));
    assert(!con("<=2.0").satisfied_by(ver("2.0.1")));
    for (const char* op : {">=", "=", "<", ">", "<="}) {
        auto r = VersionConstraint::parse(op);
        assert(!r.ok() && r.error().category == ttmod::errcat::kSyntax &&
               r.error().message == "missing version after operator");
    }
    for (const char* malformed : {"==1.0", ">=>=1.0", ">= 1.0"}) assert(!VersionConstraint::parse(malformed).ok());
    assert(!ttmod::VersionConstraint::parse(">=x.y").ok());

    // Hashing matches equality (used as an unordered key elsewhere).
    std::unordered_set<ModId> set;
    set.insert(a);
    set.insert(a2);
    set.insert(b);
    assert(set.size() == 2 && set.count(a2) == 1);
    assert(std::hash<ModId>{}(a) == std::hash<ModId>{}(a2));

    // Default-constructed is an explicit invalid placeholder, not an id.
    {
        ModId empty;
        assert(!empty.valid() && empty.str().empty());
        assert(!(empty == a));
    }

    // The validated manifest can only carry a valid id.
    {
        auto m = ttmod::parse_manifest("{\"id\":\"ok.mod\",\"api\":1}").value();
        assert(m.identity.id.valid() && m.identity.id == ModId::parse("ok.mod").value());
        assert(!ttmod::parse_manifest("{\"id\":\"../evil\",\"api\":1}").ok());
        assert(!ttmod::parse_manifest("{\"id\":1,\"api\":1}").ok());
        // Dependency and conflict ids are validated at the same boundary.
        assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"depends\":[{\"id\":\"..\"}]}").ok());
        assert(!ttmod::parse_manifest("{\"id\":\"m\",\"api\":1,\"conflicts\":[\"..\"]}").ok());
    }

    std::puts("modid: all asserts passed");
    return 0;
}