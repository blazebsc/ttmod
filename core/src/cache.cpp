// Package cache sync. See cache.hpp.
#include "ttmod/cache.hpp"
#include "ttmod/package.hpp"

#include <cstdio>
#include <filesystem>

namespace ttmod {
namespace {

namespace fs = std::filesystem;

std::string read_file(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char b[4096];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) s.append(b, r);
    fclose(f);
    return s;
}

bool write_file(const std::string& p, const std::string& s) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    size_t w = fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    return w == s.size();
}

std::string marker_for(const std::string& package_path) {
    std::error_code ec;
    auto sz = fs::file_size(package_path, ec);
    if (ec) return "";
    auto mt = fs::last_write_time(package_path, ec);
    if (ec) return "";
    return "size=" + std::to_string(sz) + " mtime=" + std::to_string(mt.time_since_epoch().count());
}

} // namespace

CacheSync sync_package_cache(const std::string& cache_dir,
                             const std::vector<std::pair<std::string, std::string>>& packaged) {
    CacheSync out;
    std::error_code ec;
    fs::create_directories(cache_dir, ec);
    if (ec) {
        out.ok = false;
        out.error = "cannot create cache dir";
        return out;
    }
    // Wanted ids (for stale cleanup).
    std::map<std::string, bool> wanted;
    for (auto& [id, pkg] : packaged) {
        wanted[id] = true;
        std::string dir = (fs::path(cache_dir) / id).string();
        std::string marker_path = (fs::path(dir) / ".ttmod-cache").string();
        std::string want_marker = marker_for(pkg);
        std::string have_marker = read_file(marker_path);
        if (!want_marker.empty() && have_marker == want_marker && fs::is_directory(dir, ec)) {
            out.effective[id] = dir;
            continue; // fresh
        }
        fs::remove_all(dir, ec);
        std::string err;
        if (!extract_package(pkg, dir, err)) {
            out.log.push_back(id + ": cache refresh failed: " + err);
            continue;
        }
        if (!write_file(marker_path, marker_for(pkg))) {
            out.log.push_back(id + ": cache marker unwritable");
            fs::remove_all(dir, ec);
            continue;
        }
        out.log.push_back(id + ": cached from package");
        out.effective[id] = dir;
    }
    // Stale cleanup: our-marker dirs with no source package go away.
    for (auto& e : fs::directory_iterator(cache_dir, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        std::string marker = (e.path() / ".ttmod-cache").string();
        if (!fs::exists(marker, ec)) continue; // not ours: never touch
        std::string id = e.path().filename().string();
        if (wanted.find(id) == wanted.end()) {
            fs::remove_all(e.path(), ec);
            out.log.push_back(id + ": stale cache removed");
        }
    }
    return out;
}

} // namespace ttmod
