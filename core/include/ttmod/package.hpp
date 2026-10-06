#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ttmod/manifest.hpp" // kPackageFormat
#include "ttmod/package_policy.hpp"
#include "ttmod/result.hpp"

namespace ttmod {

// .ttmod = ZIP containing a mod directory (manifest.json at root).
// All functions are portable (miniz, vendored) and never execute content.

struct PackEntry {
    std::string name; // archive path, '/' separators, no leading '/'
    uint64_t size = 0;
};

struct PackView {
    std::string manifest_text; // raw manifest.json bytes
    std::vector<PackEntry> files; // validated regular files (excl. manifest)
};

// Open + validate without extracting. Checks: archive integrity, safe entry
// paths (no traversal/absolute/drive/UNC/extended-prefix), no symlinks or
// special files, exactly one root manifest.json (case-insensitive dup
// detection), normalized-name dedupe, and (against the parsed manifest)
// presence of every declared files{} target + plugin path.
Result<PackView> inspect_package(const std::string& path);

// Extract a validated package into dest_dir (created if missing; must be
// empty or nonexistent). Re-validates during extraction; cleans up on error.
Result<void> extract_package(const std::string& path, const std::string& dest_dir);

// Create a deterministic package from a source mod dir. Entry order sorted,
// fixed timestamp, fixed compression level. Failure carries a structured
// Error (missing manifest, unsafe names, IO error).
Result<void> create_package(const std::string& src_dir, const std::string& out_path);

} // namespace ttmod
