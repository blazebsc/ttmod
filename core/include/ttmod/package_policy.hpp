#pragma once
// Package resource limits (Step: package safety). One place, no drift.
//
// These bound what an untrusted .ttmod can cost us in disk, memory and
// inodes. They are deliberately generous for real mod content (a voice
// archive is tens of MB) while still refusing the classic archive bombs:
// too many entries, one huge entry, and total expansion far beyond the
// compressed size.
//
// Policy notes:
// - kMaxTotalUncompressedBytes is the real anti-bomb limit. Compression
//   ratio is unbounded in principle, so total expansion is checked even
//   though the compressed package is already capped.
// - Path length and depth are bounded here as well as in
//   validate_mod_relative_path; the archive layer checks the RAW entry
//   name (before normalization) because a name can be pathological while
//   normalizing to something short.
// - Limits are compiled in, not configurable: a mod author cannot widen
//   them, and a user cannot accidentally disable them.
#include <cstdint>

namespace ttmod::packlimits {

// Whole .ttmod file on disk.
inline constexpr uint64_t kMaxPackageBytes = 512ull << 20; // 512 MiB
// Entries in the central directory.
inline constexpr uint32_t kMaxEntries = 4096;
// manifest.json after decompression.
inline constexpr uint64_t kMaxManifestBytes = 1ull << 20; // 1 MiB
// One decompressed entry. Mod content is Lua, config, and small DLLs; the
// game's own voice/asset archives are not mod content and never pass here.
inline constexpr uint64_t kMaxEntryBytes = 32ull << 20; // 32 MiB
// Sum of all decompressed entries (the real anti-bomb limit).
inline constexpr uint64_t kMaxTotalUncompressedBytes = 96ull << 20; // 96 MiB
// Raw entry name, checked before normalization.
inline constexpr size_t kMaxPathChars = 512;
// Components in one entry path.
inline constexpr size_t kMaxPathDepth = 32;

} // namespace ttmod::packlimits