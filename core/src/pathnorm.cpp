#include "ttmod/pathnorm.hpp"
#include <cctype>
#include <vector>

namespace ttmod {
namespace {

bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

} // namespace

std::string normalize_win_path(const std::string& raw) {
    std::string s = raw;
    // 1. extended prefixes (both slash styles), longest first.
    static const char* kPrefixes[] = {"\\\\?\\UNC\\", "//?/UNC/", "\\\\?\\", "//?/", "\\??\\", "/??/"};
    for (auto* p : kPrefixes) {
        if (starts_with(s, p)) {
            s = (p[4] == 'U' || p[4] == 'u') ? std::string("//") + s.substr(8) : s.substr(4);
            break;
        }
    }
    // 2. separators.
    for (char& c : s)
        if (c == '\\') c = '/';
    // 3-5. split, process components. Root floor (never popped):
    // drive "x:" -> 1 component, UNC "//srv/share" -> 2, rooted -> 0 anchored
    // by leading '/', relative -> ".." preserved.
    bool unc = starts_with(s, "//");
    bool rooted = !s.empty() && s[0] == '/';
    // "x:" drive prefix (rooted "x:/..." or drive-relative "x:...") is an
    // unpoppable floor component.
    std::string drive;
    if (!unc && s.size() >= 2 && s[1] == ':' &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) {
        drive = s.substr(0, 2);
        drive[0] = (char)tolower((unsigned char)drive[0]);
        s = s.substr(2);
        rooted = rooted || (!s.empty() && s[0] == '/');
    }
    size_t floor = 0;
    std::vector<std::string> parts;
    if (!drive.empty()) {
        parts.push_back(drive);
        floor = 1;
    } else if (unc) {
        floor = 2; // //server/share
    }
    std::string cur;
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = i < s.size() ? s[i] : '/';
        if (c == '/') {
            // Windows strips trailing dots/spaces of each component
            // (except navigation tokens).
            if (cur != "." && cur != "..") {
                while (!cur.empty() && (cur.back() == '.' || cur.back() == ' ')) cur.pop_back();
            }
            if (cur.empty() || cur == ".") {
                // skip
            } else if (cur == "..") {
                if (parts.size() > floor) parts.pop_back();
                else if (floor == 0 && !rooted) parts.push_back("..");
                // else: above root -> drop (floor)
            } else {
                parts.push_back(cur);
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    std::string o;
    if (unc) o = "//";
    else if (rooted && drive.empty()) o = "/";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) o += '/';
        o += parts[i];
    }
    // 6. lowercase ASCII.
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    // 7. trailing '/' (keep bare roots).
    while (o.size() > 1 && o.back() == '/' && o != "//") o.pop_back();
    if (o.empty()) o = "/";
    if (!drive.empty() && parts.size() == 1) o += "/"; // bare "c:" -> "c:/"
    return o;
}

std::optional<std::string> relative_key(const std::string& norm_abs, const std::string& norm_root) {
    if (norm_abs == norm_root) return std::string("");
    if (norm_abs.size() > norm_root.size() && starts_with(norm_abs, norm_root) &&
        norm_abs[norm_root.size()] == '/')
        return norm_abs.substr(norm_root.size() + 1);
    return std::nullopt;
}

std::optional<std::string> join_checked(const std::string& norm_mod_dir, const std::string& rel) {
    if (rel.empty()) return std::nullopt;
    // Manifest values must be relative subpaths: no drive/UNC/absolute forms.
    // (':' never occurs in a legal relative component; also blocks "c:/..." smuggling.)
    if (rel.find(':') != std::string::npos) return std::nullopt;
    if (rel[0] == '/' || rel[0] == '\\') return std::nullopt;
    if (rel.size() > 512) return std::nullopt;
    std::string joined = normalize_win_path(norm_mod_dir + "/" + rel);
    if (joined == norm_mod_dir) return joined;
    if (joined.size() > norm_mod_dir.size() && starts_with(joined, norm_mod_dir) &&
        joined[norm_mod_dir.size()] == '/')
        return joined;
    return std::nullopt; // escaped
}

} // namespace ttmod
