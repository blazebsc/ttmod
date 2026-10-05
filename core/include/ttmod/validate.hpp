#pragma once
#include <string>
#include "ttmod/result.hpp"

namespace ttmod {

// Canonical mod ID policy (Stage B): 1-64 chars, ASCII letters/digits plus
// '_', '-', '.'. No slashes, no backslashes, no colons, no "..", never "."
// or ".." itself, no leading/trailing whitespace. IDs cross into filesystem
// paths and runtime state, so this runs on every entry point.
bool is_valid_mod_id(std::string_view id);

// Canonical mod-relative path policy (Stage B): rejects absolute paths,
// drive-qualified paths, UNC paths, and any ".." traversal. Returns the
// normalized relative path (forward slashes) or a structured Error.
// Use for plugin paths, replacement file paths, package entries - both
// packaged and unpacked mods follow these same rules.
Result<std::string> validate_mod_relative_path(std::string_view path);

} // namespace ttmod
