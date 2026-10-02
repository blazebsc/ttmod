#include "ttmod/logmap.hpp"
#include "ttmod/ttarch.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // ttarch header: synthetic valid
    {
        char cmd[256];
        snprintf(cmd, sizeof cmd, "rm -rf /tmp/opencode_ttmod_arch && mkdir -p /tmp/opencode_ttmod_arch");
        assert(system(cmd) == 0);
        FILE* f = fopen("/tmp/opencode_ttmod_arch/a.ttarch2", "wb");
        assert(f);
        uint8_t hdr[64] = {};
        memcpy(hdr, "ZCTT", 4);
        hdr[4] = 0;
        hdr[5] = 0;
        hdr[6] = 1;
        hdr[7] = 0;
        hdr[8] = 3;
        // q = {44, 1171, 12530} LE
        hdr[12] = 44;
        hdr[20] = 1171 & 0xFF;
        hdr[21] = (1171 >> 8) & 0xFF;
        hdr[28] = 12530 & 0xFF;
        hdr[29] = (12530 >> 8) & 0xFF;
        memset(hdr + 36, 0xAB, 16);
        fwrite(hdr, 1, 64, f);
        fwrite("payload", 1, 7, f);
        fclose(f);
        auto h = ttmod::inspect_ttarch2("/tmp/opencode_ttmod_arch/a.ttarch2");
        assert(h.ok && h.file_size == 71);
        assert(memcmp(h.magic, "ZCTT", 4) == 0);
        assert(h.w[1] == 1 && h.w[2] == 3);
        assert(h.q[0] == 44 && h.q[1] == 1171 && h.q[2] == 12530);
        assert(h.tag[0] == 0xAB && h.tag[15] == 0xAB);
        // too small + bad magic + missing
        FILE* f2 = fopen("/tmp/opencode_ttmod_arch/b.ttarch2", "wb");
        fwrite("tiny", 1, 4, f2);
        fclose(f2);
        assert(!ttmod::inspect_ttarch2("/tmp/opencode_ttmod_arch/b.ttarch2").ok);
        FILE* f3 = fopen("/tmp/opencode_ttmod_arch/c.ttarch2", "wb");
        uint8_t hb[64] = {};
        memcpy(hb, "NOPE", 4);
        fwrite(hb, 1, 64, f3);
        fclose(f3);
        assert(!ttmod::inspect_ttarch2("/tmp/opencode_ttmod_arch/c.ttarch2").ok);
        assert(!ttmod::inspect_ttarch2("/tmp/opencode_ttmod_arch/nope.ttarch2").ok);
    }
    // logmap: synthetic log
    {
        std::string log =
            "[INFO] boot\n"
            "[INFO] [CreateFileW#1] \\\\?\\H:\\g\\archives\\_resdesc_50_Boot.lua\n"
            "[INFO] [CreateFileW#2] \\\\?\\H:\\g\\archives\\MCSM_pc_Menu_ms.ttarch2\n"
            "[INFO] [CreateFileW#3] \\\\?\\H:\\g\\archives\\MCSM_pc_Menu_ms.ttarch2\n"
            "[INFO] [CreateFileW#4] C:\\u\\Documents\\Telltale Games\\X\\prefs.prop\n"
            "[INFO] [CreateFileW#5] C:\\w\\x.dll\n";
        auto m = ttmod::map_boot_log(log);
        assert(m.ok && m.total == 5);
        assert(m.by_ext[".lua"] == 1 && m.by_ext[".ttarch2"] == 2 && m.by_ext[".prop"] == 1 &&
               m.by_ext[".dll"] == 1);
        assert(m.by_category["resdesc"] == 1 && m.by_category["archive"] == 2 &&
               m.by_category["save"] == 1 && m.by_category["other"] == 1);
        assert(m.resdesc_order.size() == 1 && m.archive_order.size() == 1); // dedupe
        assert(m.save_paths.size() == 1);
        assert(ttmod::map_boot_log("no opens here").total == 0);
    }
    std::puts("offline: all asserts passed");
    return 0;
}
