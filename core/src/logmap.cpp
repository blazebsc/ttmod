// Boot-log map. See logmap.hpp.
#include "ttmod/logmap.hpp"
#include "ttmod/gamestate.hpp" // classify_category: the ONE live+offline decision
#include "ttmod/pathnorm.hpp"

namespace ttmod {

void logmap_feed(LogMap& m, const std::string& normalized_path) {
    const std::string& norm = normalized_path;
    if (norm.empty()) return;
    auto seen = [&](const std::vector<std::string>& v, const std::string& s) {
        for (auto& e : v)
            if (e == s) return true;
        return false;
    };
    m.total++;
    auto dot = norm.find_last_of('.');
    auto slash = norm.find_last_of('/');
    std::string ext = (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                          ? norm.substr(dot)
                          : "(none)";
    m.by_ext[ext]++;
    // No local override: the category comes from classify_category(), the same
    // decision the live GameTracker applies, so replays cannot disagree.
    std::string cat = classify_category(norm);
    m.by_category[cat]++;
    if (cat == "resdesc" && !seen(m.resdesc_order, norm)) m.resdesc_order.push_back(norm);
    if (cat == "archive" && !seen(m.archive_order, norm)) m.archive_order.push_back(norm);
    if (cat == "save" && !seen(m.save_paths, norm)) m.save_paths.push_back(norm);
}

LogMap map_from_paths(const std::vector<std::string>& normalized_paths) {
    LogMap m;
    for (auto& p : normalized_paths) logmap_feed(m, p);
    return m;
}

LogMap map_boot_log(const std::string& log_text) {
    LogMap m;
    size_t pos = 0;
    while (true) {
        size_t f = log_text.find("CreateFileW#", pos);
        if (f == std::string::npos) break;
        size_t b = log_text.find("] ", f);
        if (b == std::string::npos) break;
        size_t e = log_text.find('\n', b);
        std::string path = log_text.substr(b + 2, e == std::string::npos ? e : e - b - 2);
        while (!path.empty() && (path.back() == '\r' || path.back() == ' ')) path.pop_back();
        pos = e == std::string::npos ? log_text.size() : e + 1;
        if (path.empty()) continue;
        // Text layer ends here: normalize, then all counting via structs.
        logmap_feed(m, normalize_win_path(path));
    }
    return m;
}

} // namespace ttmod
