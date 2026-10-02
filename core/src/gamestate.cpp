#include "ttmod/gamestate.hpp"
#include "ttmod/events.hpp" // classify_path: the single path classifier
#include <cstdio> // snprintf (detach-safe rendering, no heap)

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

std::string classify_category(const std::string& normalized) {
    if (is_save_file(normalized)) return "save"; // is_save_file takes basename or full path
    return classify_path(normalized);
}

void GameTracker::feed(const std::string& /*category*/, const std::string& normalized) {
    std::string cat = classify_category(normalized);
    if (cat == "save") {
        saves_++;
        if (save_dir_.empty()) {
            auto s = normalized.find_last_of('/');
            save_dir_ = s == std::string::npos ? "" : normalized.substr(0, s);
        }
        return;
    }
    if (cat == "archive") archives_++;
    else if (cat == "resdesc") resdesc_++;
    else others_++;
    int ep = parse_episode(basename(normalized));
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

int format_snapshot_json(const GameSnapshot& s, char* buf, size_t len) {
    if (!buf || !len) return -1;
    char eps[64] = {};
    size_t pos = 0;
    for (size_t i = 0; i < s.episodes_seen.size() && pos + 1 < sizeof eps; ++i) {
        int n = snprintf(eps + pos, sizeof eps - pos, "%s%d", i ? "," : "",
                         s.episodes_seen[i]);
        if (n < 0) break;
        if ((size_t)n >= sizeof eps - pos) break; // truncated, eps stays NUL-ended
        pos += (size_t)n;
    }
    // Normalized paths use '/'; escape only '"' and '\\'. Truncate on control
    // bytes (never valid in a normalized path) and at the buffer floor.
    char dir[256] = {};
    size_t dp = 0;
    for (char c : s.save_dir) {
        if (dp + 2 >= sizeof dir) break;
        if (c == '"' || c == '\\') {
            dir[dp++] = '\\';
            dir[dp++] = c;
        } else if ((unsigned char)c < 0x20) {
            break;
        } else {
            dir[dp++] = c;
        }
    }
    return snprintf(buf, len,
                    "{\"episodes\":[%s],\"archives\":%d,\"resdesc\":%d,"
                    "\"saves\":%d,\"others\":%d,\"save_dir\":\"%s\"}\r\n",
                    eps, s.archives_opened, s.resdesc_opened, s.saves_observed,
                    s.others_opened, dir);
}

} // namespace ttmod
