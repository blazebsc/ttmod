// ModPlan: one resolution, one load order, one "is this loadable" query.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ttmod/modplan.hpp"

using namespace ttmod;

static ModManifest mk(const char* id, const char* ver, const char* api = "1") {
    std::string text =
        std::string("{\"id\":\"") + id + "\",\"version\":\"" + ver + "\",\"api\":" + api + ",\"files\":{}}";
    auto m = parse_manifest(text);
    assert(m.ok());
    return m.value();
}

static Discovery disc_of(std::vector<ModManifest> mods) {
    Discovery d;
    for (auto& m : mods)
        d.mods.push_back({m.identity.id, "/mods/" + m.identity.id.str(), false, ModSourceKind::Directory, m});
    return d;
}

int main() {
    // Dependency-first ordering, not id order.
    {
        auto d = disc_of({mk("zebra", "1.0"), mk("apple", "1.0"), mk("base", "1.0")});
        d.mods[2].manifest.deps.depends.clear();
        d.mods[0].manifest.deps.depends = {{ModId::parse("base").value(), ""}};
        ModPlan p = build_plan(d, CacheSync{});
        assert(p.load_order.size() == 3);
        assert(p.find(ModId::parse("base").value()) != nullptr);
        // "dependency-first" means every dependency precedes its dependents,
        // not that dependencies precede independent mods.
        size_t base_at = 0, zebra_at = 0;
        for (size_t i = 0; i < p.load_order.size(); ++i) {
            if (p.load_order[i].id == ModId::parse("base").value()) base_at = i;
            if (p.load_order[i].id == ModId::parse("zebra").value()) zebra_at = i;
        }
        assert(base_at < zebra_at);
    }
    // Blocked mods are excluded from load order but reported.
    {
        auto d = disc_of({mk("needs.ghost", "1.0"), mk("fine", "1.0")});
        d.mods[0].manifest.deps.depends = {{ModId::parse("ghost").value(), ""}};
        ModPlan p = build_plan(d, CacheSync{});
        assert(p.load_order.size() == 1);
        assert(p.load_order[0].id == ModId::parse("fine").value());
        assert(p.blocked(ModId::parse("needs.ghost").value()));
        assert(p.blocked_reason(ModId::parse("needs.ghost").value()).find("missing") != std::string::npos);
        assert(p.blocked_mods.size() == 1);
    }
    // Safe mode blocks everything but keeps it visible.
    {
        auto d = disc_of({mk("a", "1.0"), mk("b", "1.0")});
        ModPlan p = build_plan(d, CacheSync{}, ModPlanOptions{true});
        assert(p.load_order.empty());
        assert(p.blocked_mods.size() == 2);
        assert(p.blocked(ModId::parse("a").value()));
    }
    // A packaged mod with no cache entry is dropped, not pointed at a
    // missing directory.
    {
        Discovery d;
        auto m = mk("pkg.mod", "1.0");
        d.mods.push_back({m.identity.id, "/mods/pkg.mod.ttmod", true, ModSourceKind::Package, m});
        ModPlan p = build_plan(d, CacheSync{}); // empty cache
        assert(p.load_order.empty());
        assert(!p.skipped.empty());
        CacheSync cs;
        cs.effective["pkg.mod"] = "/cache/pkg.mod";
        ModPlan p2 = build_plan(d, cs);
        assert(p2.load_order.size() == 1);
        assert(p2.load_order[0].dir == "/cache/pkg.mod");
        assert(p2.load_order[0].packaged);
    }
    // Determinism: identical input yields identical order.
    {
        auto d = disc_of({mk("m1", "1.0"), mk("m2", "1.0"), mk("m3", "1.0")});
        ModPlan a = build_plan(d, CacheSync{});
        ModPlan b = build_plan(d, CacheSync{});
        assert(a.load_order.size() == b.load_order.size());
        for (size_t i = 0; i < a.load_order.size(); ++i) assert(a.load_order[i].id == b.load_order[i].id);
    }
    std::puts("modplan: all asserts passed");
    return 0;
}