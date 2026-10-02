#pragma once
#include <map>
#include <string>
#include <vector>

namespace ttmod {

// .ttmod cache sync (portable). Packaged mods extract to
// <cache_dir>/<id>/ so the runtime consumes one uniform shape (unpacked
// dirs are used in place). Freshness = package size+mtime marker; stale
// cache entries (marker present, source gone) are removed. Directories
// without our marker are never touched.
struct CacheSync {
    // id -> effective dir (cache dir for packages). Empty when nothing synced.
    std::map<std::string, std::string> effective;
    std::vector<std::string> log; // human lines for diagnostics
    bool ok = true;
    std::string error;
};

// packaged: vector of (mod id, package file path). Returns sync result.
CacheSync sync_package_cache(const std::string& cache_dir,
                             const std::vector<std::pair<std::string, std::string>>& packaged);

} // namespace ttmod
