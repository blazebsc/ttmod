#pragma once
#include <map>
#include <string>
#include <vector>
#include "ttmod/result.hpp"

namespace ttmod {

// .ttmod cache sync (portable). Packaged mods extract to
// <cache_dir>/<id>/ so the runtime consumes one uniform shape (unpacked
// dirs are used in place). Freshness = content hash + size + schema marker;
// stale cache entries (marker present, source gone) are removed.
// Directories without our marker are never touched. Extraction is
// transactional (temp dir + rename): a crash never leaves a partial cache
// that looks complete.
inline constexpr int kCacheSchema = 1;
struct CacheSync {
    // id -> effective dir (cache dir for packages). Empty when nothing synced.
    std::map<std::string, std::string> effective;
    std::vector<std::string> log; // human lines for diagnostics
};

// packaged: vector of (mod id, package file path). Per-mod failures are
// logged, not fatal; only cache-dir creation failure is a hard Error.
Result<CacheSync> sync_package_cache(const std::string& cache_dir,
                                     const std::vector<std::pair<std::string, std::string>>& packaged);

} // namespace ttmod
