// ModId: validated strong type. Invalid ids must not be constructible,
// valid ids must round-trip exactly, and ordering must be stable (the
// dependency graph and discovery both sort by it).
#include <cassert>
#include <cstdio>
#include <string>
#include <unordered_set>

#include "ttmod/manifest.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/validate.hpp"

int main() {
    using ttmod::ModId;

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
    assert(!(a < a2));

    // Hashing matches equality (used as an unordered key elsewhere).
    std::unordered_set<ModId> set;
    set.insert(a);
    set.insert(a2);
    set.insert(b);
    assert(set.size() == 2 && set.count(a2) == 1);

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