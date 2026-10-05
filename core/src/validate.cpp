#include "ttmod/validate.hpp"
#include <vector>

namespace ttmod {

bool is_valid_mod_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    if (id.front() == ' ' || id.front() == '\t' || id.back() == ' ' || id.back() == '\t')
        return false;
    if (id == "." || id == "..") return false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    if (id.find("..") != std::string_view::npos) return false;
    return true;
}

static Error path_err(std::string_view path, const char* category, const char* msg) {
    return Error{"validate-path", std::string(path), category, msg};
}

Result<std::string> validate_mod_relative_path(std::string_view path) {
    if (path.empty())
        return Result<std::string>::fail(path_err(path, "missing", "empty path"));
    if (path.size() > 512)
        return Result<std::string>::fail(path_err(path, "limit", "path too long"));
    std::string p(path);
    for (char& c : p)
        if (c == '\\') c = '/';
    if (p[0] == '/')
        return Result<std::string>::fail(path_err(path, "absolute", "absolute path"));
    if (p[0] == '~')
        return Result<std::string>::fail(path_err(path, "absolute", "home-relative path"));
    if (p.size() > 1 && p[1] == ':')
        return Result<std::string>::fail(path_err(path, "absolute", "drive-qualified path"));
    if (p.find(':') != std::string::npos)
        return Result<std::string>::fail(path_err(path, "absolute", "colon path (ADS)"));
    if (p.size() > 1 && p[0] == '/' && p[1] == '/')
        return Result<std::string>::fail(path_err(path, "absolute", "UNC path"));
    // Walk components lexically: collapse "." and "/", resolve ".." by
    // popping; only popping past the root is an escape.
    std::vector<std::string> kept;
    std::string cur;
    bool bad_dotdot = false;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") {
            cur.clear();
            return;
        }
        if (cur == "..") {
            if (kept.empty()) bad_dotdot = true;
            else kept.pop_back();
        } else {
            kept.push_back(cur);
        }
        cur.clear();
    };
    for (char c : p) {
        if (c == '/') flush();
        else cur += c;
    }
    flush();
    if (bad_dotdot)
        return Result<std::string>::fail(path_err(path, "traversal", "'..' escapes mod root"));
    if (kept.empty())
        return Result<std::string>::fail(path_err(path, "missing", "empty after normalization"));
    if (kept.size() > 32)
        return Result<std::string>::fail(path_err(path, "limit", "path too deep"));
    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) out += '/';
        out += kept[i];
    }
    return Result<std::string>::ok(out);
}

} // namespace ttmod
