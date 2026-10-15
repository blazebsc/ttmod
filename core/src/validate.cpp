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

static Error path_err_s(std::string_view path, const char* category, std::string msg) {
    return Error{"validate-path", std::string(path), category, std::move(msg)};
}

// Windows filename identity (review 2026-10-16): the filesystem strips
// trailing dots/spaces and reserves device-name bases ("con.txt" IS the
// console). One check, used by both validators, so the validator and the
// actual filesystem share one notion of path identity.
static const char* const kReservedDevices[] = {"CON",  "PRN",  "AUX",  "NUL",
                                               "COM1", "COM2", "COM3", "COM4", "COM5", "COM6",
                                               "COM7", "COM8", "COM9",
                                               "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6",
                                               "LPT7", "LPT8", "LPT9"};

static bool is_windows_invalid_component(const std::string& comp) {
    if (comp.back() == '.' || comp.back() == ' ') return true;
    std::string base = comp.substr(0, comp.find('.'));
    for (char& c : base) c = (char)toupper((unsigned char)c);
    for (const char* r : kReservedDevices)
        if (base == r) return true;
    return false;
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
    // Windows filename aliases (review 2026-10-16): see
    // is_windows_invalid_component above.
    for (auto& comp : kept) {
        if (comp.back() == '.' || comp.back() == ' ')
            return Result<std::string>::fail(
                path_err(path, "alias", "component ends in dot/space (Windows alias)"));
        std::string base = comp.substr(0, comp.find('.'));
        for (char& c : base) c = (char)toupper((unsigned char)c);
        for (const char* r : kReservedDevices)
            if (base == r)
                return Result<std::string>::fail(
                    path_err_s(path, "reserved", std::string("reserved device name: ") + comp));
    }
    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) out += '/';
        out += kept[i];
    }
    return Result<std::string>::ok(out);
}

Result<std::string> validate_game_path(std::string_view path) {
    if (path.empty())
        return Result<std::string>::fail(path_err(path, "missing", "empty path"));
    if (path.size() > 512)
        return Result<std::string>::fail(path_err(path, "limit", "path too long"));
    std::string p(path);
    for (char& c : p)
        if (c == '\\') c = '/';
    if (p.rfind("//", 0) == 0)
        return Result<std::string>::fail(path_err(path, "absolute", "UNC path"));
    if (p[0] == '/')
        return Result<std::string>::fail(path_err(path, "absolute", "POSIX-absolute path"));
    // Drive-QUALIFIED ABSOLUTE is allowed ("C:/..." - the resolver checks it
    // stays under the game root). A letter-colon prefix without the slash
    // ("x:stream") is drive-relative at best and ADS-shaped at worst;
    // game keys never legitimately use it, so it falls to the colon check.
    bool drive = p.size() > 2 && p[1] == ':' && p[2] == '/' &&
                 ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'));
    if (!drive && p.find(':') != std::string::npos)
        return Result<std::string>::fail(path_err(path, "absolute", "colon path (ADS)"));
    std::string cur;
    std::vector<std::string> comps;
    auto flush = [&]() {
        if (!cur.empty() && cur != ".") comps.push_back(cur);
        cur.clear();
    };
    for (char c : p) {
        if (c == '/') flush();
        else cur += c;
    }
    flush();
    for (auto& comp : comps) {
        if (comp == "..")
            return Result<std::string>::fail(path_err(path, "traversal", "'..' in game path"));
        if (is_windows_invalid_component(comp)) {
            std::string why = comp.back() == '.' || comp.back() == ' '
                                  ? "component ends in dot/space (Windows alias)"
                                  : "reserved device name";
            return Result<std::string>::fail(path_err_s(path, "alias", why));
        }
    }
    if (comps.empty())
        return Result<std::string>::fail(path_err(path, "missing", "empty after normalization"));
    return Result<std::string>::ok(p);
}

} // namespace ttmod
