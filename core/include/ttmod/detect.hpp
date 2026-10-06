#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include "ttmod/result.hpp"

namespace ttmod {

// Minimal PE identity. No Windows headers so it builds on Linux for offline tooling.
struct ExeInfo {
    std::string path;
    uint64_t file_size = 0;
    uint16_t machine = 0;      // 0x014C=i386, 0x8664=x64
    uint16_t num_sections = 0;
    uint32_t timestamp = 0;    // COFF TimeDateStamp
    uint16_t opt_magic = 0;    // 0x10B=PE32, 0x20B=PE32+
    uint64_t fnv1a64 = 0;      // fast streaming hash (stdlib-only; SHA256 recorded externally)
};

Result<ExeInfo> parse_pe(const std::string& path);
// Layered form (Stage 12): parse bytes already in memory (no file I/O
// inside the parser). file_size/fnv1a64 stay empty - the path wrapper
// fills identity + hash around it.
Result<ExeInfo> parse_pe_bytes(std::span<const std::byte> bytes);
// FNV-1a 64 over whole file, streamed.
uint64_t fnv1a_file(const std::string& path, uint64_t* out_size = nullptr);

inline const char* arch_name(uint16_t machine) {
    return machine == 0x014C ? "x86" : machine == 0x8664 ? "x64" : "unknown";
}

} // namespace ttmod
