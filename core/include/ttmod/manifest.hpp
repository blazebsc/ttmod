#pragma once
#include <string>
#include <utility>
#include <vector>
#include "ttmod/modconfig.hpp"
#include "ttmod/profile.hpp"

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
// Responsibilities are split into nested structs (Stage C); storage lives
// here, Lua-visible shapes are built field-by-field by callers.
struct ModIdentity {
    std::string id;
    std::string version;
};
struct ModCompatibility {
    std::vector<std::string> games;
    Architecture arch = Architecture::Any; // absent = any
    int api = 0;
};
struct DependencySpec {
    // depends entries {mod id, minimum version ("" = any)}.
    std::vector<std::pair<std::string, std::string>> depends;
    // conflicts entries (mod ids that must NOT be present+enabled).
    std::vector<std::string> conflicts;
};
struct OverrideSpec {
    // game path -> mod-rel replacement.
    std::vector<std::pair<std::string, std::string>> files;
    int priority = 100; // higher wins
};
struct PluginSpec {
    // Native plugin path relative to mod root (e.g. "plugins/foo.dll");
    // absent + no plugin.dll = resource-only.
    std::string path;
};
struct ModPresentation {
    std::string name;
    std::string description;
    std::vector<ConfigOption> config;
};
struct ModManifest {
    bool ok = false;
    ModIdentity identity;
    ModCompatibility compat;
    DependencySpec deps;
    OverrideSpec overrides;
    PluginSpec plugin;
    ModPresentation presentation;
    bool enabled = true;
    // Package format version (default 1 when absent).
    int package_format = 1;
    std::string error;
};

// Package format version we write and accept (M11).
inline constexpr int kPackageFormat = 1;

ModManifest parse_manifest(const std::string& text);

// Dotted-numeric version compare: -1/0/+1. Non-numeric tails ignored
// ("1.0.0-beta" == "1.0.0" for gating). Missing parts are 0 ("1.2" == "1.2.0").
int compare_versions(const std::string& a, const std::string& b);

} // namespace ttmod
