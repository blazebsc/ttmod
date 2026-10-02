// Boot-log map. See logmap.hpp.
#include "ttmod/logmap.hpp"
#include "ttmod/events.hpp" // classify_path
#include "ttmod/gamestate.hpp" // is_save_file
#include "ttmod/pathnorm.hpp"

namespace ttmod {

LogMap map_boot_log(const std::string& log_text) {
    LogMap m;
    m.ok = true;
    size_t pos = 0;
    auto seen = [&](const std::vector<std::string>& v, const std::string& s) {
        for (auto& e : v)
            if (e == s) return true;
        return false;
    };
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
        m.total++;
        std::string norm = normalize_win_path(path);
        auto dot = norm.find_last_of('.');
        auto slash = norm.find_last_of('/');
        std::string ext = (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                              ? norm.substr(dot)
                              : "(none)";
        m.by_ext[ext]++;
        std::string base = slash == std::string::npos ? norm : norm.substr(slash + 1);
        std::string cat = classify_path(norm);
        if (is_save_file(base)) cat = "save";
        m.by_category[cat]++;
        if (cat == "resdesc" && !seen(m.resdesc_order, norm)) m.resdesc_order.push_back(norm);
        if (cat == "archive" && !seen(m.archive_order, norm)) m.archive_order.push_back(norm);
        if (cat == "save" && !seen(m.save_paths, norm)) m.save_paths.push_back(norm);
    }
    return m;
}

} // namespace ttmod
