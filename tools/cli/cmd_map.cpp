#include "commands.hpp"

#include <cstdio>
#include <string>

#include "ttmod/file_io.hpp"
#include "ttmod/logmap.hpp"

int cmd_map(int argc, char** argv) {
    if (argc != 1) return cmd_usage();
    auto m = ttmod::map_boot_log(ttmod::file_io::read_all(argv[0]));
    if (!m.ok) {
        std::puts("cannot parse log");
        return 1;
    }
    std::printf("opens: %d\nby category:", m.total);
    for (auto& [k, v] : m.by_category) std::printf(" %s=%d", k.c_str(), v);
    std::printf("\nby ext:");
    for (auto& [k, v] : m.by_ext) std::printf(" %s=%d", k.c_str(), v);
    std::printf("\nresdesc files: %u\narchives: %u\nsaves: %u\n", (unsigned)m.resdesc_order.size(),
            (unsigned)m.archive_order.size(), (unsigned)m.save_paths.size());
    return 0;
}
