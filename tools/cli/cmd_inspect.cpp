#include "commands.hpp"

#include <cstdio>
#include <string>

#include "ttmod/ttarch.hpp"

int cmd_inspect(int argc, char** argv) {
    if (argc != 1) return cmd_usage();
    auto hr = ttmod::inspect_ttarch2(argv[0]);
    if (!hr.ok()) {
        std::printf("NOT-TTARCH2: %s\n", hr.error().message.c_str());
        return 1;
    }
    auto h = hr.value();
    std::printf("magic: %.4s\nsize: %llu\nu16: %u %u %u %u\n"
                "u64: %llu %llu %llu\ntag: ",
                h.magic, (unsigned long long)h.file_size, h.w[0], h.w[1], h.w[2], h.w[3],
                (unsigned long long)h.q[0], (unsigned long long)h.q[1],
                (unsigned long long)h.q[2]);
    for (int i = 0; i < 16; ++i) std::printf("%02x", h.tag[i]);
    std::printf("\n(note: field semantics Unknown; see docs)\n");
    return 0;
}
