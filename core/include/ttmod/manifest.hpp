#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ttmod/modconfig.hpp"
#include "ttmod/profile.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/result.hpp"

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
    ModId id;
    std::string version;
};
struct ModCompatibility {
    std::vector<std::string> games;
    Architecture arch = Architecture::Any; // absent = any
    int api = 0;
};
struct DependencySpec {
    // depends entries {mod id, minimum version ("" = any)}.
    std::vector<std::pair<ModId, std::string>> depends;
    // conflicts entries (mod ids that must NOT be present+enabled).
    std::vector<ModId> conflicts;
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
// Declared execution environments + requested permissions (doc §§19-21).
// Parsed and validated here; enforcement belongs to the runtimes that
// don't exist yet (TTMod VM) or the game boundary. Unknown names are
// rejected: permissions are a security boundary, not a hint.
//
// Strongly typed past the manifest boundary (review: strings must not
// spread inward). The JSON strings convert once, here, into these enums.
enum class Runtime { Lua, Luau, TelltaleLua, Native };
enum class Permission {
    GameRead,
    GameWrite,
    GameEvents,
    UI,
    Resources,
    FilesystemRead,
    FilesystemWrite,
    ModsRead,
    ModsWrite,
    GameLua,
    GameMemory,
    Hooks,
    Native
};

inline const char* to_string(Runtime r) {
    switch (r) {
        case Runtime::Lua: return "lua";
        case Runtime::Luau: return "luau";
        case Runtime::TelltaleLua: return "telltale-lua";
        default: return "native";
    }
}

inline const char* to_string(Permission p) {
    switch (p) {
        case Permission::GameRead: return "game.read";
        case Permission::GameWrite: return "game.write";
        case Permission::GameEvents: return "game.events";
        case Permission::UI: return "ui";
        case Permission::Resources: return "resources";
        case Permission::FilesystemRead: return "filesystem.read";
        case Permission::FilesystemWrite: return "filesystem.write";
        case Permission::ModsRead: return "mods.read";
        case Permission::ModsWrite: return "mods.write";
        case Permission::GameLua: return "game.lua";
        case Permission::GameMemory: return "game.memory";
        case Permission::Hooks: return "hooks";
        default: return "native";
    }
}

struct RuntimeSpec {
    // Empty runtimes = unspecified. Empty permissions = none requested.
    std::vector<Runtime> runtimes;
    std::vector<Permission> permissions;
};

inline std::optional<Runtime> parse_runtime(std::string_view s) {
    if (s == "lua") return Runtime::Lua;
    if (s == "luau") return Runtime::Luau;
    if (s == "telltale-lua") return Runtime::TelltaleLua;
    if (s == "native") return Runtime::Native;
    return std::nullopt;
}

inline std::optional<Permission> parse_permission(std::string_view s) {
    if (s == "game.read") return Permission::GameRead;
    if (s == "game.write") return Permission::GameWrite;
    if (s == "game.events") return Permission::GameEvents;
    if (s == "ui") return Permission::UI;
    if (s == "resources") return Permission::Resources;
    if (s == "filesystem.read") return Permission::FilesystemRead;
    if (s == "filesystem.write") return Permission::FilesystemWrite;
    if (s == "mods.read") return Permission::ModsRead;
    if (s == "mods.write") return Permission::ModsWrite;
    if (s == "game.lua") return Permission::GameLua;
    if (s == "game.memory") return Permission::GameMemory;
    if (s == "hooks") return Permission::Hooks;
    if (s == "native") return Permission::Native;
    return std::nullopt;
}
struct ModManifest {
    ModIdentity identity;
    ModCompatibility compat;
    DependencySpec deps;
    OverrideSpec overrides;
    PluginSpec plugin;
    ModPresentation presentation;
    RuntimeSpec runtime;
    bool enabled = true;
    // Package format version (default 1 when absent).
    int package_format = 1;
};

// Parse + validate. Success carries the manifest; failure carries a
// structured Error (operation "parse-manifest", object = id parsed so
// far, message text kept byte-stable for existing log lines).
// Package format version we write and accept (M11).
inline constexpr int kPackageFormat = 1;

Result<ModManifest> parse_manifest(const std::string& text);

// Dotted-numeric version compare: -1/0/+1, using the bounded grammar in
// version.hpp. Unparseable versions compare as equal to "0" on that side
// (a malformed manifest version is rejected at parse time anyway); this
// stays for callers that only need an ordering.
int compare_versions(const std::string& a, const std::string& b);

} // namespace ttmod
