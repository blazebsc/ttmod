#pragma once
#include <optional>
#include <string>
#include <vector>
#include <string_view>
#include <utility>
#include <vector>
#include "ttmod/modconfig.hpp"
#include "ttmod/profile.hpp"
#include "ttmod/modid.hpp"
#include "ttmod/result.hpp"
#include "ttmod/version.hpp"

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
    Version version;
};
struct ModCompatibility {
    std::vector<std::string> games;
    Architecture arch = Architecture::Any; // absent = any
    int api = 0;
    [[nodiscard]] bool supports_api(int host_api) const noexcept;
    [[nodiscard]] bool supports_game(std::string_view game, int season) const;
    [[nodiscard]] bool supports_arch(Architecture host) const noexcept;
};
struct DependencySpec {
    // depends entries {mod id, version constraint ("" = any)}.
    std::vector<std::pair<ModId, VersionConstraint>> depends;
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
    case Runtime::Lua:
        return "lua";
    case Runtime::Luau:
        return "luau";
    case Runtime::TelltaleLua:
        return "telltale-lua";
    default:
        return "native";
    }
}

inline const char* to_string(Permission p) {
    switch (p) {
    case Permission::GameRead:
        return "game.read";
    case Permission::GameWrite:
        return "game.write";
    case Permission::GameEvents:
        return "game.events";
    case Permission::UI:
        return "ui";
    case Permission::Resources:
        return "resources";
    case Permission::FilesystemRead:
        return "filesystem.read";
    case Permission::FilesystemWrite:
        return "filesystem.write";
    case Permission::ModsRead:
        return "mods.read";
    case Permission::ModsWrite:
        return "mods.write";
    case Permission::GameLua:
        return "game.lua";
    case Permission::GameMemory:
        return "game.memory";
    case Permission::Hooks:
        return "hooks";
    default:
        return "native";
    }
}

// Per-runtime script entry points (doc §§18, 20, 65). Mod-relative paths,
// already through validate_mod_relative_path(); empty = not declared for that
// runtime. The native entry point is NOT here: it is the existing "plugin"
// field, because two sources of truth for one DLL is how a mod ends up doing
// nothing silently.
//
// Legacy tolerance: a manifest with no "entrypoints" key at all stays valid
// even if it declares script runtimes - it predates the script VM, and
// refusing would break shipped mods for no gain. Once the key IS present, the
// two fields must agree exactly (a runtime with no entry point, or an entry
// point for an undeclared runtime, is rejected).
struct Entrypoints {
    std::string lua;
    std::string luau;
    std::string telltale_lua;
    [[nodiscard]] const std::string* find(Runtime r) const noexcept;
    [[nodiscard]] bool empty() const noexcept {
        return lua.empty() && luau.empty() && telltale_lua.empty();
    }
    // Native has no entry here by design: it is the manifest "plugin" field.
    // Present so callers can ask the question without special-casing it.
    [[nodiscard]] bool native_is_absent() const noexcept {
        return true;
    }
};

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
// A validated manifest. validate_manifest establishes these invariants:
// - id is valid; versions and dependency constraints are typed.
// - plugin, replacement-target, and entrypoint paths are normalized mod-relative;
//   game-side override keys remain resolver-owned.
// - games are well-formed and unique; dependencies are non-self, unique,
//   and non-contradictory.
// - runtimes and permissions are known and unique; entrypoints agree with
//   runtimes when present; the config schema is validated.
// Consumers rely on these invariants and never re-validate them.
struct ModManifest {
    ModIdentity identity;
    ModCompatibility compat;
    DependencySpec deps;
    OverrideSpec overrides;
    PluginSpec plugin;
    ModPresentation presentation;
    RuntimeSpec runtime;
    Entrypoints entrypoints;
    bool enabled = true;
    // Package format version (default 1 when absent).
    int package_format = 1;
};

// Package format version we write and accept (M11).
inline constexpr int kPackageFormat = 1;

// Unknown fields: IGNORED, never rejected. An older TTMod must load a
// manifest written by a newer one, which is the whole point of additive
// optional fields ("priority", "enabled", "runtimes", "permissions" were
// all added after v1 and old readers shipped). RawManifest records the
// names in unknown_fields for tooling; a future package_format bump is
// where strictness would be introduced.

// ---- Layer 1: syntax (untrusted shape) ----------------------------------
//
// RawManifest is what the JSON bytes actually said: every field is an
// optional<string> / raw value, with NO semantic validation. Type errors
// are captured as field-level notes rather than aborting, so validation
// can report the complete picture rather than the first problem.
//
// Unknown keys are recorded in unknown_fields and IGNORED (additive
// compatibility: an older TTMod must load a manifest written by a newer
// one). A future major format may reject them.
struct RawManifest {
    std::optional<std::string> id, version, name, description, plugin, arch;
    std::optional<int> api, priority, package_format;
    std::optional<bool> enabled;
    std::optional<std::vector<std::string>> games, conflicts;
    std::optional<std::vector<std::string>> runtimes, permissions;
    struct RawDep {
        std::string id;
        std::string version; // "" = unconstrained
    };
    std::optional<std::vector<RawDep>> depends;
    // game path (as written) -> mod-relative replacement, both unvalidated
    std::optional<std::vector<std::pair<std::string, std::string>>> files;
    // Config schema and entrypoints kept as JSON text; each has its own
    // validation stage, so neither is decoded twice here.
    std::optional<std::string> config_json;
    std::optional<std::string> entrypoints_json;
    std::vector<std::string> unknown_fields;
    std::vector<std::string> notes; // "files[0].to: bad type"
};

// text -> JSON -> RawManifest. Syntax problems only (bad JSON, wrong JSON
// types, oversize input); no id/path/version semantics.
Result<RawManifest> read_raw_manifest(const std::string& text);

// ---- Layer 2: semantics -------------------------------------------------

// RawManifest -> validated ModManifest. Enforces required fields, ids,
// paths, versions, runtimes, permissions, package format, and delegates
// config-schema validation. Returns Result<ModManifest>: a partially
// invalid manifest never escapes this function.
Result<ModManifest> validate_manifest(const RawManifest& raw);

// Convenience: both layers, in order. This is what every caller wants.
Result<ModManifest> parse_manifest(const std::string& text);

} // namespace ttmod
