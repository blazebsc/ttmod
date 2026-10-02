#pragma once
#include <cstdint>
#include <string>

namespace ttmod {

// Minimal PE identity. No Windows headers so it builds on Linux for offline tooling.
struct ExeInfo {
    bool ok = false;
    std::string path;
    uint64_t file_size = 0;
    uint16_t machine = 0;      // 0x014C=i386, 0x8664=x64
    uint16_t num_sections = 0;
    uint32_t timestamp = 0;    // COFF TimeDateStamp
    uint16_t opt_magic = 0;    // 0x10B=PE32, 0x20B=PE32+
    uint64_t fnv1a64 = 0;      // fast streaming hash (stdlib-only; SHA256 recorded externally)
    std::string error;
};

ExeInfo parse_pe(const std::string& path);
// FNV-1a 64 over whole file, streamed.
uint64_t fnv1a_file(const std::string& path, uint64_t* out_size = nullptr);

inline const char* arch_name(uint16_t machine) {
    return machine == 0x014C ? "x86" : machine == 0x8664 ? "x64" : "unknown";
}

} // namespace ttmod
