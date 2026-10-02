#include "ttmod/gamestate.hpp"

namespace ttmod {

std::string GameTracker::basename(const std::string& n) {
    auto s = n.find_last_of('/');
    return s == std::string::npos ? n : n.substr(s + 1);
}

int parse_episode(const std::string& normalized_basename) {
    // find "minecraft" + 3 digits, e.g. minecraft106 -> 6 (MCSM1 101..108)
    for (size_t i = 0; i + 12 < normalized_basename.size() + 1; ++i) {
        if (normalized_basename.compare(i, 9, "minecraft") != 0) continue;
        char a = normalized_basename[i + 9], b = normalized_basename[i + 10],
             c = normalized_basename[i + 11];
        if (a == '1' && b == '0' && c >= '1' && c <= '8') return c - '0';
    }
    return 0;
}

bool is_save_file(const std::string& normalized_basename) {
    std::string b = normalized_basename;
    auto s = b.find_last_of('/');
    if (s != std::string::npos) b = b.substr(s + 1);
    if (b == "prefs.prop" || b == "elfdl.prop") return true;
    if (b.size() > 8 && b.compare(0, 8, "session_") == 0 &&
        b.compare(b.size() - 7, 7, ".estore") == 0)
        return true;
    return false;
}

void GameTracker::feed(const std::string& category, const std::string& normalized) {
    std::string b = basename(normalized);
    if (is_save_file(b)) {
        saves_++;
        if (save_dir_.empty()) {
            auto s = normalized.find_last_of('/');
            save_dir_ = s == std::string::npos ? "" : normalized.substr(0, s);
        }
        return;
    }
    if (category == "archive") archives_++;
    else if (category == "resdesc") resdesc_++;
    else others_++;
    int ep = parse_episode(b);
    if (ep > 0) {
        for (int e : episodes_)
            if (e == ep) return;
        episodes_.push_back(ep);
    }
}

GameSnapshot GameTracker::snapshot() const {
    GameSnapshot s;
    s.episodes_seen = episodes_;
    s.archives_opened = archives_;
    s.resdesc_opened = resdesc_;
    s.saves_observed = saves_;
    s.others_opened = others_;
    s.save_dir = save_dir_;
    return s;
}

} // namespace ttmod
