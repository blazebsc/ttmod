// libFuzzer harness: manifest + config parsing (Stage I). Build with
// -DTTMOD_BUILD_FUZZ=ON (Clang only). Run: ./fuzz_manifest corpus/ -max_total_time=60
#include <cstddef>
#include <cstdint>
#include "ttmod/manifest.hpp"
#include "ttmod/modconfig.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 65536) return 0;
    std::string input((const char*)data, size);
    auto m = ttmod::parse_manifest(input);
    (void)m.ok();
    (void)ttmod::parse_config_schema(input);
    (void)ttmod::parse_config_file(input);
    return 0;
}
