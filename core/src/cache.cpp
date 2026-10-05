// Package cache sync. See cache.hpp.
#include "ttmod/cache.hpp"
#include "ttmod/detect.hpp"
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
    // Identity: content hash + size + schema. Never mtime (unreliable).
    // Old size+mtime markers never match -> one clean re-extract.
    uint64_t size = 0;
    uint64_t hash = fnv1a_file(package_path, &size);
    if (hash == 0) return "";
    char m[96];
    snprintf(m, sizeof m, "v=%d fnv=%016llx size=%llu", kCacheSchema,
             (unsigned long long)hash, (unsigned long long)size);
    return m;
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
        // Transactional: extract to a temp dir, validate, then rename into
        // place. A crash mid-extract leaves temp junk, never a partial cache.
        std::string tmp = dir + ".tmp-ttmod";
        fs::remove_all(tmp, ec);
        std::string err;
        if (!extract_package(pkg, tmp, err) || !write_file((fs::path(tmp) / ".ttmod-cache").string(),
                                                           marker_for(pkg))) {
            if (err.empty()) err = "cache marker unwritable";
            out.log.push_back(id + ": cache refresh failed: " + err);
            fs::remove_all(tmp, ec);
            continue;
        }
        fs::remove_all(dir, ec);
        fs::rename(tmp, dir, ec);
        if (ec) {
            out.log.push_back(id + ": cache replace failed: " + ec.message());
            fs::remove_all(tmp, ec);
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
