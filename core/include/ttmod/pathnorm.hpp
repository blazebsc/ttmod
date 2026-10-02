#pragma once
#include <optional>
#include <string>

namespace ttmod {

// Canonical form for Windows paths (comparison keys, resolver index).
// Normalization performed (documented, deterministic):
//  1. strip extended prefixes: "\\?\", "\\?\UNC\", "\??\" (either slash style)
//  2. '\' -> '/'
//  3. collapse redundant '/' (a leading "//" UNC root is preserved)
//  4. drop "/./" components
//  5. resolve "/../" lexically (pop previous component; leading ".." that
//     would escape the root are dropped — root is a floor, not an error;
//     escape PREVENTION happens in join_checked, not here)
//  6. ASCII A-Z -> a-z (Windows filesystems compare case-insensitively)
//  7. strip trailing '/' except a bare root ("c:/", "//srv/share", "/")
//  8. strip trailing '.'/' ' of each component (Windows filename semantics)
//
// Examples:
//   "\\\\?\\H:\\A\\B\\..\\C.TXT" -> "h:/a/c.txt"
//   "C:\\Games\\MCSM\\\\archives\\.\\x.lua" -> "c:/games/mcsm/archives/x.lua"
std::string normalize_win_path(const std::string& raw);

// Game-root-relative key: normalized abs path minus normalized root prefix.
// Returns nullopt when abs is not under root. "" means abs == root.
std::optional<std::string> relative_key(const std::string& norm_abs,
                                        const std::string& norm_root);

// Join mod_dir (normalized abs) + rel (manifest value), normalize, and verify
// the result stays inside mod_dir. Manifest values must be relative subpaths
// (no drive letters, colons, UNC, or leading separators); anything else is
// rejected. Returns nullopt on escape or malformed rel.
// NOTE: lexical only; symlinks/reparse points can still redirect at runtime
// (documented limitation, checked where the OS permits).
std::optional<std::string> join_checked(const std::string& norm_mod_dir,
                                        const std::string& rel);

} // namespace ttmod
