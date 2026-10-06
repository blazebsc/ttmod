#pragma once
#include <cstdint>
#include <string>
#include "ttmod/result.hpp"

namespace ttmod {

// TTARCH2 header probe (M12). Reads the fixed header, checks magic, exposes
// raw fields WITHOUT claiming semantics (field meanings are Unknown — the
// archive payload format is future work; TelltaleToolKit/TTG-Tools are the
// references). Observed on MCSM1 (MCSM_pc_Boot_data.ttarch2):
//   magic "ZCTT", u16[4]={0,1,3,0}, u64-ish {44,1171,12530}, 16-byte tag.
struct Ttarch2Header {
    char magic[4] = {};
    uint16_t w[4] = {};
    uint64_t q[3] = {};
    uint8_t tag[16] = {};
    uint64_t file_size = 0;
};

Result<Ttarch2Header> inspect_ttarch2(const std::string& path);

} // namespace ttmod
