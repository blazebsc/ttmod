#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace ttmod {

// Read-only game-lifecycle state derived SOLELY from observed file events
// (no memory reading, no polling). Everything here is "seen loading", never
// "current value": e.g. episodes_seen = episode archives observed opening.
// Deeper state (scene/characters/dialogue/variables) is Unknown — see docs.
struct GameSnapshot {
    std::vector<int> episodes_seen; // e.g. {1,2,6}, first-seen order
    int archives_opened = 0;
    int resdesc_opened = 0;
    int saves_observed = 0;
    int others_opened = 0;
    std::string save_dir; // normalized dir of first save-like file seen
};

// Episode number from an archive/resdesc basename, e.g.
// "mcsm_pc_minecraft106_ms.ttarch2" -> 6, "_resdesc_50_minecraft102_data" -> 2.
// Returns 0 when the name carries no episode identity. MCSM1-only pattern
// (101..108); other games need their own parsers (MCSM2 constraint honored).
int parse_episode(const std::string& normalized_basename);

// Save-like file? prefs.prop / elfdl.prop / session_*.estore (observed names).
bool is_save_file(const std::string& normalized_basename);

// Single category decision shared by the live tracker and the offline logmap:
// classify_path with the save-file override ("resdesc" | "archive" | "save" |
// "other"). Note the event ABI category stays classify_path-only ("save" is
// not a valid event category — see plugin_api.h); the tracker applies the
// override internally so the two tallies cannot disagree.
std::string classify_category(const std::string& normalized);

class GameTracker {
public:
    // Feed one observed open. The category argument is accepted for call-site
    // convenience but NOT trusted: the tally re-derives it via
    // classify_category(normalized), so a disagreeing caller cannot skew live
    // counts away from the offline replay.
    void feed(const std::string& category, const std::string& normalized);
    GameSnapshot snapshot() const;

private:
    std::vector<int> episodes_;
    int archives_ = 0, resdesc_ = 0, saves_ = 0, others_ = 0;
    std::string save_dir_;
    static std::string basename(const std::string& n);
};

// Structured rendering of a snapshot: the primary detach artifact in
// ttmod.exit.log, written next to the human-readable state: line (which stays
// byte-identical for grep). snprintf-only into the caller buffer, no heap, so
// the Windows detach path can use it directly. Returns the snprintf count
// (negative on bad args). Ends with CRLF like the rest of the exit log.
int format_snapshot_json(const GameSnapshot& s, char* buf, size_t len);

} // namespace ttmod
