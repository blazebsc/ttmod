// Step 1: Result/Error model — success, every error category, propagation,
// and the no-exceptions boundary (safe APIs never throw).
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "ttmod/cache.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/file_io.hpp"
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
        auto string_result = Result<std::string>::ok("kept");
        assert(string_result.value_or("fallback") == "kept");
        assert(string_result.value() == "kept");
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
        auto attributed = ttmod::with_operation(Error{"inner", "object", ttmod::errcat::kSyntax, "message"}, "outer");
        assert(attributed.operation == "outer" && attributed.object == "object" &&
               attributed.category == ttmod::errcat::kSyntax && attributed.message == "message");
    } catch (...) {
        assert(!"failure path threw");
    }

    {
        auto held = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(42));
        std::unique_ptr<int> extracted = std::move(held).value();
        assert(extracted && *extracted == 42);

        auto held_or = Result<std::unique_ptr<int>>::ok(std::make_unique<int>(17));
        std::unique_ptr<int> extracted_or = std::move(held_or).value_or(nullptr);
        assert(extracted_or && *extracted_or == 17);

        auto failed_or = Result<std::unique_ptr<int>>::fail(Error{"op", "", ttmod::errcat::kMissing, "missing"});
        std::unique_ptr<int> fallback = std::move(failed_or).value_or(nullptr);
        assert(!fallback);

        auto mutable_result = Result<int>::ok(1);
        int* value = mutable_result.try_value();
        assert(value);
        *value = 2;
        assert(mutable_result.value() == 2);

        auto failed_try = Result<int>::fail(Error{"op", "", ttmod::errcat::kIO, "failed"});
        assert(failed_try.try_value() == nullptr);
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
    assert(!ttmod::sync_package_cache("/tmp/opencode_ttmod_result_cache",
                                      {{ttmod::ModId::parse("ghost").value(), "/nonexistent-ttmod-pkg"}})
                .value()
                .effective.count(ttmod::ModId::parse("ghost").value())); // per-mod failure is logged, not fatal
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

    // Windows filename aliases (review 2026-10-16): the FS strips trailing
    // dots/spaces and reserves device-name bases, so these must be REJECTED -
    // "payload.dll." must not count as a distinct, safe entry.
    assert(!ttmod::validate_mod_relative_path("payload.dll.").ok());   // trailing dot alias
    assert(!ttmod::validate_mod_relative_path("file.lua ").ok());      // trailing space alias
    assert(!ttmod::validate_mod_relative_path("con.txt").ok());         // reserved base
    assert(!ttmod::validate_mod_relative_path("archives/NUL").ok());    // reserved base, nested
    assert(!ttmod::validate_mod_relative_path("COM1").ok());            // bare device
    assert(!ttmod::validate_mod_relative_path("aux.lua").ok());         // case-insensitive
    assert(ttmod::validate_mod_relative_path("payload.dll").ok());      // clean still passes
    assert(ttmod::validate_mod_relative_path("conduct.lua").ok());      // prefix, not alias
    // GamePath: traversal, ADS, POSIX/UNC absolutes, aliases rejected;
    // drive-absolute passes through (the resolver checks game-root).
    assert(!ttmod::validate_game_path("archives/../x").ok());           // traversal
    assert(!ttmod::validate_game_path("x:stream").ok());                // ADS
    assert(!ttmod::validate_game_path("/etc/passwd").ok());             // POSIX-absolute
    assert(!ttmod::validate_game_path("//srv/share").ok());            // UNC
    assert(!ttmod::validate_game_path("con.txt").ok());                 // reserved
    assert(ttmod::validate_game_path("archives/x.lua").ok());           // root-relative
    assert(ttmod::validate_game_path("C:/games/mcsm/archives/x.lua").ok()); // drive-absolute

    // Bounded whole-file read (review 2026-10-16): distinct outcomes, never
    // a truncated prefix - the failure mode the old reader had.
    {
        const std::string p = "/tmp/opencode_ttmod_result_read";
        FILE* f = fopen(p.c_str(), "wb");
        assert(f && fputs("hello script", f) >= 0);
        fclose(f);
        auto rd = ttmod::file_io::read_file_bounded(p, 1024);
        assert(rd.ok() && rd.value() == "hello script");
        auto over = ttmod::file_io::read_file_bounded(p, 4); // 12 > 4
        assert(!over.ok());
        assert(std::string(over.error().category) == ttmod::errcat::kLimit); // NOT a prefix
        auto gone = ttmod::file_io::read_file_bounded(p + ".missing", 1024);
        assert(!gone.ok() && std::string(gone.error().category) == ttmod::errcat::kIO);
        f = fopen(p.c_str(), "wb"); // empty file: readable, distinct from unreadable
        assert(f);
        fclose(f);
        auto empty = ttmod::file_io::read_file_bounded(p, 1024);
        assert(empty.ok() && empty.value().empty());
        std::remove(p.c_str());
    }

    std::puts("result: all asserts passed");
    return 0;
}
