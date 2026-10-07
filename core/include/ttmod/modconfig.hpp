#pragma once
// Per-mod configuration: schema (from manifest "config"), values (from
// config/<id>.json), validation, deterministic serialization, and the Lua
// table-literal builder consumed by the native Mods menu. Portable, tested.
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "ttmod/result.hpp"

namespace ttmod {

// Schema entry. type is one of: bool int float string enum color.
// "color" is a string restricted to "#RRGGBB" (editable via the native menu).
struct ConfigOption {
    std::string key;
    std::string type;
    std::string label;
    bool def_bool = false;
    long long def_int = 0;
    double def_float = 0.0;
    std::string def_str;
    double min_val = 0.0;
    double max_val = 0.0;
    bool has_min = false;
    bool has_max = false;
    double step = 0.0;
    std::vector<std::string> options; // enum choices
};

struct ConfigValue {
    enum class Type { NONE, BOOL, INT, FLOAT, STR } type = Type::NONE;
    bool b = false;
    long long i = 0;
    double f = 0.0;
    std::string s;
    static ConfigValue boolean(bool v) {
        ConfigValue c;
        c.type = Type::BOOL;
        c.b = v;
        return c;
    }
    static ConfigValue integer(long long v) {
        ConfigValue c;
        c.type = Type::INT;
        c.i = v;
        return c;
    }
    static ConfigValue number(double v) {
        ConfigValue c;
        c.type = Type::FLOAT;
        c.f = v;
        return c;
    }
    static ConfigValue text(const std::string& v) {
        ConfigValue c;
        c.type = Type::STR;
        c.s = v;
        return c;
    }
};

// Parse one manifest "config" array (JSON) into schema options.
// Strict: a broken schema fails the manifest (a broken schema disables
// config UI, not the mod — enforced by the caller).
Result<std::vector<ConfigOption>> parse_config_schema(const std::string& json);

// Parse a flat config file {"key": bool|int|float|string}. Unknown keys
// kept (forward-compatible); malformed file -> error (caller: defaults).
struct ConfigFile {
    std::map<std::string, ConfigValue> values;
};
Result<ConfigFile> parse_config_file(const std::string& text);

// Default value for an option.
ConfigValue config_default(const ConfigOption& o);

// True iff v is acceptable for o (type + range + enum membership +
// string length cap). Invalid file values fall back to config_default.
bool config_validate(const ConfigOption& o, const ConfigValue& v);

// Merge file values over schema defaults (schema order irrelevant).
std::map<std::string, ConfigValue> config_effective(const std::vector<ConfigOption>& schema,
                                                    const ConfigFile& file);

// Pure state transitions for the menu setters (file I/O stays caller-side,
// fully unit-testable here).
// Enable/disable: rewrite of config/mods.json content.
Result<std::string> apply_enabled_change(const std::string& mods_json, const std::string& id, bool enabled);
// Config value: coerce valstr per the option type (bool: 1/0/true/false;
// int/float: strict numeric; string/enum: raw, with "" resetting strings
// to default), validate, merge over the existing file content.
Result<std::string> apply_config_value(const std::vector<ConfigOption>& schema,
                                           const std::string& file_text, const std::string& key,
                                           const std::string& valstr);

// Deterministic serialization (schema order, then extras sorted).
std::string serialize_config(const std::vector<ConfigOption>& schema,
                             const std::map<std::string, ConfigValue>& values);

// Menu model layering (Stage H):
//   Config Schema -> Config Values -> Config Store -> MenuModSnapshot
//   (this model, Lua-independent) -> build_menu_literal (Lua serializer).
// Future UI frontends consume the model, never the literal. The menu
// consumes ONLY snapshots below.
struct MenuModSnapshot {
    std::string id; // serialized via ModId::str() at the boundary
    std::string name;
    std::string version;
    std::string description;
    bool enabled = true;
    bool has_plugin = false;
    bool packaged = false;
    std::vector<ConfigOption> schema;
    std::map<std::string, ConfigValue> values; // effective
};

// Build `ttmod_menu={seq=N,mods={...}}` literal. Strings Lua-escaped;
// control chars stripped. seq lets Lua detect staleness.
std::string build_menu_literal(const std::vector<MenuModSnapshot>& mods, unsigned seq);

} // namespace ttmod
