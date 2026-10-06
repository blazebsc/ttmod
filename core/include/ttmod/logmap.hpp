#pragma once
#include <map>
#include <string>
#include <vector>

namespace ttmod {

// Boot-log map (M12): summarizes CreateFileW# lines from ttmod.log —
// the same analysis previously done by hand. Pure parsing, portable.
//
// Status: FALLBACK for forensics on logs that predate the structured snapshot
// in ttmod.exit.log. The snapshot (GameTracker::snapshot(), rendered by
// format_snapshot_json()) is the primary artifact; this path stays working
// for old logs only. Live code must never grep logs.
//
// Decoupling rule: log text is only an extractor. All counting lives in
// logmap_feed() over a NORMALIZED path via classify_category() — the same
// single decision the live tracker applies. Live code must prefer
// map_from_paths()/logmap_feed() and never grep logs.
struct LogMap {
    int total = 0;
    std::map<std::string, int> by_ext; // ".lua" -> 153 (lowercased, with dot)
    std::map<std::string, int> by_category; // resdesc/archive/other/save
    std::vector<std::string> resdesc_order; // first-seen normalized paths
    std::vector<std::string> archive_order; // first-seen normalized paths
    std::vector<std::string> save_paths; // first-seen normalized paths
};

// Struct-fed counting: single tally site for one normalized path.
void logmap_feed(LogMap& m, const std::string& normalized_path);

// Struct-fed builder: no log text involved (live/offline without parsing).
LogMap map_from_paths(const std::vector<std::string>& normalized_paths);

LogMap map_boot_log(const std::string& log_text);

} // namespace ttmod
