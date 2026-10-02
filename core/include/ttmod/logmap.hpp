#pragma once
#include <map>
#include <string>
#include <vector>

namespace ttmod {

// Boot-log map (M12): summarizes CreateFileW# lines from ttmod.log —
// the same analysis previously done by hand. Pure parsing, portable.
struct LogMap {
    bool ok = false;
    std::string error;
    int total = 0;
    std::map<std::string, int> by_ext; // ".lua" -> 153 (lowercased, with dot)
    std::map<std::string, int> by_category; // resdesc/archive/other/save
    std::vector<std::string> resdesc_order; // first-seen normalized paths
    std::vector<std::string> archive_order; // first-seen normalized paths
    std::vector<std::string> save_paths; // first-seen normalized paths
};

LogMap map_boot_log(const std::string& log_text);

} // namespace ttmod
