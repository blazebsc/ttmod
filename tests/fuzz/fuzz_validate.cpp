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
    (void)ttmod::Version(input).compare(ttmod::Version("1.0"));
    (void)ttmod::VersionConstraint(input).satisfied_by(ttmod::Version("1.0"));
    return 0;
}
