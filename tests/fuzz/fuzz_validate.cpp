// libFuzzer harness: validators + version parsing (Stage I).
#include <cstddef>
#include <cstdint>
#include <string>
#include "ttmod/validate.hpp"
#include "ttmod/version.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024) return 0;
    std::string input((const char*)data, size);
    (void)ttmod::is_valid_mod_id(input);
    auto r = ttmod::validate_mod_relative_path(input);
    (void)r.ok();
    // Parse, do not construct: Version/VersionConstraint are parse-only since
    // ADR-007, so there is no such thing as a Version made of invalid text.
    auto ver = ttmod::Version::parse(input);
    if (!ver.ok()) return 0;
    auto base = ttmod::Version::parse("1.0");
    if (!base.ok()) return 0; // constant input, so it always parses
    (void)ver.value().compare(base.value());
    auto cons = ttmod::VersionConstraint::parse(input);
    if (!cons.ok()) return 0;
    (void)cons.value().satisfied_by(base.value());
    return 0;
}
