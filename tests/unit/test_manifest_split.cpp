// Step 4: the manifest parse/validate split.
// Layer 1 (read_raw_manifest) must reject only syntax and JSON type errors,
// and must NOT apply semantic policy. Layer 2 (validate_manifest) owns ids,
// paths, versions, runtimes, permissions and package format.
#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/manifest.hpp"

int main() {
    using ttmod::RawManifest;

    // Layer 1 accepts semantically-bad-but-well-typed input.
    {
        auto r = ttmod::read_raw_manifest("{\"id\":\"../evil\",\"api\":0,\"version\":\"1.x\"}");
        assert(r.ok()); // not our job to judge semantics here
        const RawManifest& raw = r.value();
        assert(raw.id && *raw.id == "../evil");
        assert(raw.api && *raw.api == 0);
    }

    // Layer 1 rejects wrong JSON types (accumulated, not first-error only).
    {
        auto bad = ttmod::read_raw_manifest("{\"id\":1,\"api\":\"x\"}");
        assert(!bad.ok());
        assert(bad.error().category == ttmod::errcat::kType);
        auto multi = ttmod::read_raw_manifest("{\"id\":1,\"api\":1,\"priority\":[],\"enabled\":\"y\"}");
        assert(!multi.ok());
        // id, priority, enabled are all wrong-typed: reported together.
        assert(multi.error().message.find("3 field problems") != std::string::npos ||
               multi.error().message == "3 field problems");
        auto arr = ttmod::read_raw_manifest("[1,2]");
        assert(!arr.ok() && arr.error().category == ttmod::errcat::kType);
        auto files = ttmod::read_raw_manifest("{\"id\":\"a\",\"files\":{\"x\":5}}");
        assert(!files.ok());
        auto cfg = ttmod::read_raw_manifest("{\"id\":\"a\",\"config\":{}}");
        assert(!cfg.ok());
        auto dep = ttmod::read_raw_manifest("{\"id\":\"a\",\"depends\":[{\"version\":\"1\"}]}");
        assert(!dep.ok());
    }

    // Unknown fields are recorded, not rejected (additive compatibility).
    {
        auto r = ttmod::read_raw_manifest("{\"id\":\"a\",\"api\":1,\"future\":1,\"also\":{\"x\":2}}");
        assert(r.ok());
        assert(r.value().unknown_fields.size() == 2);
        assert(r.value().unknown_fields[0] == "future");
    }

    // Layer 2 owns every semantic rule.
    {
        auto v = [](const char* s) { return ttmod::parse_manifest(s); };
        // Required fields.
        assert(!v("{\"api\":1}").ok());    // no id
        assert(!v("{\"id\":\"a\"}").ok()); // no api
        // Id policy.
        assert(!v("{\"id\":\"../evil\",\"api\":1}").ok());
        assert(!v("{\"id\":\"..\",\"api\":1}").ok());
        assert(!v("{\"id\":\"\",\"api\":1}").ok());
        // Version grammar.
        assert(!v("{\"id\":\"a\",\"api\":1,\"version\":\"1.x\"}").ok());
        assert(!v("{\"id\":\"a\",\"api\":1,\"version\":\"9999999999\"}").ok());
        assert(v("{\"id\":\"a\",\"api\":1,\"version\":\"\"}").ok()); // 0.0.0
        // api range.
        assert(!v("{\"id\":\"a\",\"api\":0}").ok());
        assert(!v("{\"id\":\"a\",\"api\":-1}").ok());
        // Architecture.
        assert(!v("{\"id\":\"a\",\"api\":1,\"arch\":\"z80\"}").ok());
        assert(v("{\"id\":\"a\",\"api\":1,\"arch\":\"x64\"}").ok());
        // Package format: future writers are refused, 0 is invalid.
        assert(!v("{\"id\":\"a\",\"api\":1,\"package_format\":99}").ok());
        assert(!v("{\"id\":\"a\",\"api\":1,\"package_format\":0}").ok());
        // Plugin and replacement paths use the canonical path validator.
        assert(!v("{\"id\":\"a\",\"api\":1,\"plugin\":\"../evil.dll\"}").ok());
        assert(!v("{\"id\":\"a\",\"api\":1,\"plugin\":\"C:/x.dll\"}").ok());
        assert(v("{\"id\":\"a\",\"api\":1,\"plugin\":\"\"}").ok()); // resource-only
        assert(!v("{\"id\":\"a\",\"api\":1,\"files\":{\"g\":\"../evil\"}}").ok());
        assert(v("{\"id\":\"a\",\"api\":1,\"files\":{\"g\":\"files/x\"}}").ok());
        // Runtimes and permissions: unknown names rejected (typo safety).
        assert(!v("{\"id\":\"a\",\"api\":1,\"runtimes\":[\"ps5\"]}").ok());
        assert(v("{\"id\":\"a\",\"api\":1,\"runtimes\":[\"native\",\"lua\"]}").ok());
        assert(!v("{\"id\":\"a\",\"api\":1,\"permissions\":[\"sudo\"]}").ok());
        // Dependencies + constraints.
        assert(!v("{\"id\":\"a\",\"api\":1,\"depends\":[{\"id\":\"../x\"}]}").ok());
        assert(!v("{\"id\":\"a\",\"api\":1,\"depends\":[{\"id\":\"b\",\"version\":\">=x.y\"}]}").ok());
        assert(v("{\"id\":\"a\",\"api\":1,\"depends\":[{\"id\":\"b\",\"version\":\">=1.0\"}]}").ok());
        // Conflicts.
        assert(!v("{\"id\":\"a\",\"api\":1,\"conflicts\":[\"..\"]}").ok());
        // Config schema is validated by its own stage, once.
        assert(!v("{\"id\":\"a\",\"api\":1,\"config\":[{\"key\":\"k\"}]}").ok());
    }

    // The split is composable: hand-built RawManifest goes straight to
    // validation without re-parsing JSON.
    {
        RawManifest raw;
        raw.id = "hand.built";
        raw.api = 3;
        auto m = ttmod::validate_manifest(raw);
        assert(m.ok());
        assert(m.value().identity.id.str() == "hand.built" && m.value().compat.api == 3);
        raw.id = "../evil";
        assert(!ttmod::validate_manifest(raw).ok());
    }

    // No partially-invalid manifest escapes: failures never carry a value.
    {
        auto r = ttmod::parse_manifest("{\"id\":\"a\",\"api\":1,\"arch\":\"z80\"}");
        assert(!r.ok() && r.try_value() == nullptr);
        // Semantic failures are attributable to the validation layer.
        assert(r.error().operation == "validate-manifest");
        assert(r.error().category == ttmod::errcat::kType);
        // Syntax failures keep the parse-manifest name callers log against.
        auto syntax = ttmod::parse_manifest("{oops");
        assert(!syntax.ok() && syntax.error().operation == "parse-manifest");
    }

    std::puts("manifest-split: all asserts passed");
    return 0;
}