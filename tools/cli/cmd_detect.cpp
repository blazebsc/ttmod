#include "commands.hpp"

#include <cstdio>
#include <string>

#include "ttmod/detect.hpp"
#include "ttmod/profile.hpp"

int cmd_detect(int argc, char** argv) {
    if (argc != 1) return cmd_usage();
    ttmod::ExeInfo e = ttmod::parse_pe(argv[0]);
    if (!e.ok) {
        std::printf("NOT-PE: %s\n", e.error.c_str());
        return 1;
    }
    ttmod::GameProfile p = ttmod::select_profile(e);
    std::printf("size: %llu\nmachine: 0x%04X (%s)\ntimestamp: %u\nfnv1a64: 0x%016llX\n"
                "profile: %s game=%s season=%d status=%s\n",
                (unsigned long long)e.file_size, e.machine, ttmod::arch_name(e.machine), e.timestamp,
                (unsigned long long)e.fnv1a64, p.id, p.game, p.season, ttmod::to_string(p.status));
    return 0;
}
