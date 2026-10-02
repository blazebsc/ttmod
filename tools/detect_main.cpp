// ttmod-detect: offline game detection. Usage: ttmod-detect <exe>
#include "ttmod/detect.hpp"
#include "ttmod/profile.hpp"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("Usage: ttmod-detect <exe>\n");
        return 2;
    }
    ttmod::ExeInfo e = ttmod::parse_pe(argv[1]);
    if (!e.ok) {
        std::printf("NOT-PE: %s\n", e.error.c_str());
        return 1;
    }
    std::printf("path: %s\n", e.path.c_str());
    std::printf("size: %llu\n", (unsigned long long)e.file_size);
    std::printf("machine: 0x%04X (%s)\n", e.machine, ttmod::arch_name(e.machine));
    std::printf("timestamp: %u\n", e.timestamp);
    std::printf("opt_magic: 0x%04X\n", e.opt_magic);
    std::printf("fnv1a64: 0x%016llX\n", (unsigned long long)e.fnv1a64);
    std::printf("sections: %u\n", e.num_sections);
    // Profile via shared portable selector (same path the Windows DLL uses).
    ttmod::GameProfile prof = ttmod::select_profile(e);
    std::printf("profile: %s game=%s season=%d status=%s\n",
                prof.id, prof.game, prof.season, prof.status);
    return 0;
}
