#pragma once
#include <map>
#include <string>
#include <vector>
#include "ttmod/result.hpp"

namespace ttmod {

// .ttmod cache sync (portable). Packaged mods extract to
// <cache_dir>/<id>/ so the runtime consumes one uniform shape (unpacked
// dirs are used in place). Freshness = content hash + size + schema +
// package format marker; stale cache entries (marker present, source gone)
// are removed. Directories without our marker are never touched.
//
// Replacement is transactional (see cache.cpp): extract to
// <id>.ttmod-new, validate, mark, then swap. The live <id> directory is
// only disturbed once the new one is complete, and a crash mid-swap is
// recovered on the next run by the startup sweep.
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
