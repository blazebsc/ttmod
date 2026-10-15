#pragma once
#include <string>
#include "ttmod/result.hpp"

namespace ttmod {

// Canonical mod ID policy (Stage B): 1-64 chars, ASCII letters/digits plus
// '_', '-', '.'. No slashes, no backslashes, no colons, no "..", never "."
// or ".." itself, no leading/trailing whitespace. IDs cross into filesystem
// paths and runtime state, so this runs on every entry point.
bool is_valid_mod_id(std::string_view id);

// Canonical mod-relative path policy. Returns the normalized relative path
// (forward slashes) or a structured Error. Use for plugin paths, replacement
// file paths, package entries, config paths and any future asset/script
// field - packaged and unpacked mods follow these same rules. No module may
// invent its own traversal check.
//
// ".." policy (decided, documented, tested - see tests/security):
//   ALLOW safe lexical normalization: "a/../b" -> "b", "a/./b" -> "a/b",
//   "files\\x.lua" -> "files/x.lua". Authors write redundant segments all
//   the time and rejecting them buys nothing.
//   REJECT any path that would escape the mod root: "../evil",
//   "a/../../evil", "..\\..\\evil" fail with category "traversal". Popping
//   past the root is an escape, never a silent clamp.
//
// Also rejected: empty, absolute ("/x"), drive-qualified ("C:/x", "C:\\x"),
// UNC and extended prefixes ("//server/share", "\\\\?\\C:\\x"), home-relative
// ("~/x"), any colon (ADS), >512 chars, >32 components.
//
// Lexical only: a symlink or reparse point inside a mod directory can still
// redirect at runtime. Checked where the OS permits (documented limitation).
Result<std::string> validate_mod_relative_path(std::string_view path);

// Game-side override key (the `from` side of "files"): root-relative or
// drive-absolute. Dedicated rule (review 2026-10-16): rejects traversal
// ('..' components), ADS colons, POSIX/UNC absolutes, and Windows
// alias/reserved names - the resolver then only has to check that
// absolute keys stay under the game root. Canonical output is
// forward-slashed; drive-qualified keys pass through for the resolver's
// root check.
Result<std::string> validate_game_path(std::string_view path);
} // namespace ttmod
