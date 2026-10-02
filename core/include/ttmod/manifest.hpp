#pragma once
#include <string>
#include <utility>
#include <vector>
#include "ttmod/modconfig.hpp"

namespace ttmod {

// Unified mod manifest (native plugins, resource mods, hybrids).
// {
//   "id": "a.b", "version": "1.0.0", "api": 1,
//   "games": ["minecraft-story-mode:s1"],
//   "priority": 200,            // optional, default 100 (higher wins)
//   "enabled": true,            // optional, default true
//   "files": {                  // optional: game path -> mod-rel replacement
//     "archives/x.lua": "files/archives/x.lua"
//   }
// }
// Game paths may be root-relative ("archives/x") or absolute; replacements
// must be relative subpaths of the mod dir (enforced by join_checked).
struct ModManifest {
    bool ok = false;
    std::string id;
    std::string version;
    int api = 0;
    std::vector<std::string> games;
    int priority = 100;
    bool enabled = true;
    std::vector<std::pair<std::string, std::string>> files;
    // M10: depends entries {mod id, minimum version ("" = any)},
    // conflicts entries (mod ids that must NOT be present+enabled).
    std::vector<std::pair<std::string, std::string>> depends;
    std::vector<std::string> conflicts;
    // M11: optional native plugin path relative to mod root
    // (e.g. "plugins/foo.dll"); absent + no plugin.dll = resource-only.
    std::string plugin;
    // M11: optional CPU architecture for the plugin ("x86", "x64", "any").
    std::string arch;
    // M11: package format version (default 1 when absent).
    int package_format = 1;
    // Display/config (optional, additive): human name + description for the
    // Mods screen, and a config schema consumed by the native config UI.
    std::string name;
    std::string description;
    std::vector<ConfigOption> config;
    std::string error;
};

// Package format version we write and accept (M11).
inline constexpr int kPackageFormat = 1;

ModManifest parse_manifest(const std::string& text);

// Dotted-numeric version compare: -1/0/+1. Non-numeric tails ignored
// ("1.0.0-beta" == "1.0.0" for gating). Missing parts are 0 ("1.2" == "1.2.0").
int compare_versions(const std::string& a, const std::string& b);

} // namespace ttmod
