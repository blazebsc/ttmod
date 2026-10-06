// Step 1: Result/Error model — success, every error category, propagation,
// and the no-exceptions boundary (safe APIs never throw).
#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/cache.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/modconfig.hpp"
#include "ttmod/modstate.hpp"
#include "ttmod/package.hpp"
#include "ttmod/pathnorm.hpp"
#include "ttmod/result.hpp"
#include "ttmod/sigmatch.hpp"
#include "ttmod/theme_color.hpp"
#include "ttmod/ttarch.hpp"
#include "ttmod/validate.hpp"

int main() {
    using ttmod::Error;
    using ttmod::Result;

    // Success: all safe inspectors agree, nothing throws.
    try {
        auto ok = Result<int>::ok(42);
        assert(ok.ok() && ok.has_value());
        assert(ok.try_value() && *ok.try_value() == 42);
        assert(ok.value() == 42);
        assert(ok.value_or(7) == 42);
        auto vok = Result<void>::success();
        assert(vok.ok() && vok.has_value());
    } catch (...) {
        assert(!"success path threw");
    }

    // Failure: safe inspectors work without touching the aborting accessors.
    try {
        auto bad = Result<int>::fail(Error{"op", "obj", ttmod::errcat::kRange, "msg"});
        assert(!bad.ok() && !bad.has_value());
        assert(bad.try_value() == nullptr);
        assert(bad.value_or(7) == 7);
        assert(bad.error().operation == "op" && bad.error().object == "obj" &&
               bad.error().category == ttmod::errcat::kRange && bad.error().message == "msg");
        auto vbad = Result<void>::fail(Error{"op", "", ttmod::errcat::kIO, "io fail"});
        assert(!vbad.ok() && vbad.error().category == ttmod::errcat::kIO);
        auto made = ttmod::make_error("o", "b", ttmod::errcat::kMissing, "m");
        assert(made.operation == "o" && made.category == ttmod::errcat::kMissing);
    } catch (...) {
        assert(!"failure path threw");
    }

    // Every major category surfaces through a real core API.
    assert(!ttmod::parse_manifest("").ok());                            // syntax
    assert(!ttmod::parse_state("{oops").ok());                          // syntax
    assert(!ttmod::validate_mod_relative_path("../evil").ok());         // traversal
    assert(!ttmod::parse_pe("/nonexistent-ttmod-pe").ok());             // io
    assert(!ttmod::inspect_ttarch2("/nonexistent-ttmod-ttarch2").ok()); // io
    assert(!ttmod::inspect_package("/nonexistent-ttmod-package").ok()); // limit (size probe)
    assert(!ttmod::parse_config_file("{oops").ok());                    // syntax
    assert(!ttmod::apply_config_value({}, "", "nope", "1").ok());       // range (unknown key)
    assert(!ttmod::join_checked("c:/mods/a", "../../escape").ok());     // traversal
    assert(!ttmod::parse_signature("ZZ").ok());                         // syntax
    assert(!ttmod::parse_accent("blue").ok());                          // syntax
    assert(!ttmod::apply_enabled_change("{oops", "a.b", false).ok());   // syntax
    auto missing_digest = ttmod::fnv1a_file("/nonexistent-ttmod-hash");
    assert(!missing_digest.ok() && missing_digest.error().category == ttmod::errcat::kIO); // io
    assert(missing_digest.error().operation == "hash-file" &&
           missing_digest.error().object == "/nonexistent-ttmod-hash");
    assert(!ttmod::sync_package_cache("/tmp/opencode_ttmod_result_cache", {{"ghost", "/nonexistent-ttmod-pkg"}})
                .value()
                .effective.count("ghost")); // per-mod failure is logged, not fatal
    auto synced = ttmod::sync_package_cache("/tmp/opencode_ttmod_result_cache", {});
    assert(synced.ok()); // hard failure only on cache-dir creation

    const std::string digest_path = "/tmp/opencode_ttmod_result_hash";
    std::remove(digest_path.c_str());
    FILE* digest_file = std::fopen(digest_path.c_str(), "wb");
    assert(digest_file);
    std::fclose(digest_file);
    auto empty_digest = ttmod::fnv1a_file(digest_path);
    assert(empty_digest.ok() && empty_digest.value().size == 0);
    assert(empty_digest.value().fnv1a64 == 14695981039346656037ull);
    digest_file = std::fopen(digest_path.c_str(), "wb");
    assert(digest_file);
    assert(std::fwrite("abc", 1, 3, digest_file) == 3);
    std::fclose(digest_file);
    auto digest = ttmod::fnv1a_file(digest_path);
    assert(digest.ok() && digest.value().size == 3);
    assert(digest.value().fnv1a64 == 0xe71fa2190541574bull);
    std::remove(digest_path.c_str());

    // Categories are distinct strings (no ad-hoc aliasing).
    assert(std::string(ttmod::errcat::kSyntax) != ttmod::errcat::kIO);
    assert(std::string(ttmod::errcat::kTraversal) != ttmod::errcat::kMissing);
    assert(std::string(ttmod::errcat::kLimit) != ttmod::errcat::kDuplicate);

    std::puts("result: all asserts passed");
    return 0;
}
