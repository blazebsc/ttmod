// Per-mod configuration backend (see header).
#include "ttmod/modconfig.hpp"
#include "ttmod/json.hpp"
#include "ttmod/modstate.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ttmod {
namespace {


bool valid_type(const std::string& t) {
    return t == "bool" || t == "int" || t == "float" || t == "string" || t == "enum" ||
           t == "color";
}

// "color" values are "#RRGGBB" (upper/lower hex). Engine tinting wants an int
// elsewhere; the schema contract is the string form.
bool valid_color(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

} // namespace

Result<std::vector<ConfigOption>> parse_config_schema(const std::string& text) {
    // Strict: top-level array of objects; unknown keys skipped (additive
    // schema); duplicate keys rejected (see parse_manifest policy).
    std::vector<ConfigOption> out;
    auto fail = [](const std::string& e) { return Result<std::vector<ConfigOption>>::fail(
                                                 Error{"parse-config-schema", "", "manifest", e}); };
    auto parsed = parse_json_value(text);
    if (!parsed.ok() || !parsed.value().is_array()) return fail("config not array");
    json j = parsed.value();
    for (auto& e : j) {
        if (!e.is_object()) {
            return fail("config entry not object");
        }
        ConfigOption o;
        std::string why;
        auto get_str = [&](const char* k, std::string& dst, bool& have) {
            auto it = e.find(k);
            if (it == e.end()) return true;
            if (!it->is_string() || it->get<std::string>().empty()) {
                why = std::string("bad ") + k;
                return false;
            }
            dst = it->get<std::string>();
            have = true;
            return true;
        };
        bool have_key = false, have_type = false, have_label = false;
        if (!get_str("key", o.key, have_key) || !get_str("type", o.type, have_type) ||
            !get_str("label", o.label, have_label))
            return fail(why);
        if (!have_key || !have_type || !have_label) {
            return fail("entry missing key/type/label");
        }
        if (!valid_type(o.type)) {
            return fail("bad type");
        }
        auto num = [&](const char* k, double& dst, bool& has) {
            auto it = e.find(k);
            if (it == e.end()) return true;
            if (!it->is_number()) {
                why = std::string("bad ") + k;
                return false;
            }
            dst = it->get<double>();
            if (!std::isfinite(dst)) {
                why = std::string("bad ") + k;
                return false;
            }
            has = true;
            return true;
        };
        bool hm = false, hx = false, hs = false;
        if (!num("min", o.min_val, hm)) return fail(why);
        o.has_min = hm;
        if (!num("max", o.max_val, hx)) return fail(why);
        o.has_max = hx;
        if (!num("step", o.step, hs)) return fail(why);
        auto oit = e.find("options");
        if (oit != e.end()) {
            if (!oit->is_array()) {
                return fail("options not array");
            }
            for (auto& v : *oit) {
                if (!v.is_string()) {
                    return fail("bad options");
                }
                o.options.push_back(v.get<std::string>());
            }
        }
        if (o.type == "enum" && o.options.empty()) {
            return fail("enum needs options");
        }
        auto dit = e.find("default");
        if (dit != e.end()) {
            if (dit->is_boolean()) {
                o.def_bool = dit->get<bool>();
            } else if (dit->is_number_integer() || dit->is_number_unsigned()) {
                long long v = dit->get<long long>();
                o.def_int = v;
                o.def_float = (double)v;
            } else if (dit->is_number_float()) {
                double v = dit->get<double>();
                if (!std::isfinite(v)) {
                    return fail("bad default");
                }
                o.def_int = (long long)v;
                o.def_float = v;
            } else if (dit->is_string()) {
                o.def_str = dit->get<std::string>();
            } else {
                return fail("bad default");
            }
        }
        if (o.type == "color" && e.contains("default") && !valid_color(o.def_str)) {
            return fail("color default must be #RRGGBB");
        }
        if (!e.contains("default") && o.type == "enum") o.def_str = o.options[0];
        out.push_back(o);
    }
    return Result<std::vector<ConfigOption>>::ok(std::move(out));
}

Result<ConfigFile> parse_config_file(const std::string& text) {
    ConfigFile f;
    auto fail = [&](const std::string& msg) {
        return Result<ConfigFile>::fail(Error{"parse-config-file", "", errcat::kSyntax, msg});
    };
    // Blank -> empty (all defaults). Anything else must be a strict object;
    // malformed file -> Error (caller falls back to defaults).
    bool blank = true;
    for (char c : text) {
        if (!isspace((unsigned char)c)) {
            blank = false;
            break;
        }
    }
    if (blank) return Result<ConfigFile>::ok(std::move(f));
    auto parsed = parse_json_value(text);
    if (!parsed.ok() || !parsed.value().is_object()) {
        return fail(!parsed.ok() ? parsed.error().message : "not an object");
    }
    json j = parsed.value();
    for (auto& [k, v] : j.items()) {
        if (v.is_string()) f.values[k] = ConfigValue::text(v.get<std::string>());
        else if (v.is_boolean()) f.values[k] = ConfigValue::boolean(v.get<bool>());
        else if (v.is_number_integer() || v.is_number_unsigned()) {
            long long i = 0;
            try {
                i = v.get<long long>();
            } catch (const json::exception& e) {
                return fail(e.what());
            }
            f.values[k] = ConfigValue::integer(i);
        } else if (v.is_number_float()) {
            double d = v.get<double>();
            if (!std::isfinite(d)) {
                return fail("non-finite number");
            }
            f.values[k] = ConfigValue::number(d);
        } else {
            return fail("bad value type");
        }
    }
    return Result<ConfigFile>::ok(std::move(f));
}

ConfigValue config_default(const ConfigOption& o) {
    if (o.type == "bool") return ConfigValue::boolean(o.def_bool);
    if (o.type == "int") return ConfigValue::integer(o.def_int);
    if (o.type == "float") return ConfigValue::number(o.def_float);
    return ConfigValue::text(o.def_str);
}

bool config_validate(const ConfigOption& o, const ConfigValue& v) {
    if (o.type == "bool") return v.type == ConfigValue::Type::BOOL;
    if (o.type == "int") {
        if (v.type == ConfigValue::Type::INT) {
            double d = (double)v.i;
            if (o.has_min && d < o.min_val) return false;
            if (o.has_max && d > o.max_val) return false;
            return true;
        }
        return false;
    }
    if (o.type == "float") {
        double d = 0;
        if (v.type == ConfigValue::Type::FLOAT) d = v.f;
        else if (v.type == ConfigValue::Type::INT) d = (double)v.i;
        else return false;
        if (o.has_min && d < o.min_val) return false;
        if (o.has_max && d > o.max_val) return false;
        return true;
    }
    if (o.type == "string") {
        return v.type == ConfigValue::Type::STR && v.s.size() <= 256;
    }
    if (o.type == "color") {
        return v.type == ConfigValue::Type::STR && valid_color(v.s);
    }
    if (o.type == "enum") {
        if (v.type != ConfigValue::Type::STR) return false;
        for (auto& c : o.options)
            if (c == v.s) return true;
        return false;
    }
    return false;
}

std::map<std::string, ConfigValue> config_effective(const std::vector<ConfigOption>& schema,
                                                    const ConfigFile& file) {
    std::map<std::string, ConfigValue> out;
    for (auto& o : schema) {
        auto it = file.values.find(o.key);
        if (it != file.values.end() && config_validate(o, it->second)) out[o.key] = it->second;
        else out[o.key] = config_default(o);
    }
    return out;
}

static void append_escaped(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (u < 0x20 || u == 0x7F) continue; // strip other controls
        else out += c;
    }
    out += '"';
}

static void append_value(std::string& out, const ConfigValue& v) {
    switch (v.type) {
        case ConfigValue::Type::BOOL: out += v.b ? "true" : "false"; break;
        case ConfigValue::Type::INT: {
            char b[32];
            snprintf(b, sizeof b, "%lld", v.i);
            out += b;
            break;
        }
        case ConfigValue::Type::FLOAT: {
            char b[32];
            snprintf(b, sizeof b, "%.6g", v.f);
            out += b;
            break;
        }
        default: append_escaped(out, v.s); break;
    }
}

std::string serialize_config(const std::vector<ConfigOption>& schema,
                             const std::map<std::string, ConfigValue>& values) {
    std::string out = "{";
    bool first = true;
    auto put = [&](const std::string& k, const ConfigValue& v) {
        if (!first) out += ",";
        first = false;
        append_escaped(out, k);
        out += ":";
        append_value(out, v);
    };
    for (auto& o : schema) {
        auto it = values.find(o.key);
        put(o.key, it != values.end() ? it->second : config_default(o));
    }
    for (auto& kv : values) {
        bool in_schema = false;
        for (auto& o : schema)
            if (o.key == kv.first) {
                in_schema = true;
                break;
            }
        if (!in_schema) put(kv.first, kv.second);
    }
    out += "}";
    return out;
}

std::string build_menu_literal(const std::vector<MenuModSnapshot>& mods, unsigned seq) {
    std::string out = "ttmod_menu={seq=";
    {
        char b[32];
        snprintf(b, sizeof b, "%u", seq);
        out += b;
    }
    out += ",mods={";
    bool first = true;
    for (auto& m : mods) {
        if (!first) out += ",";
        first = false;
        out += "{id=";
        append_escaped(out, m.id);
        out += ",name=";
        append_escaped(out, m.name.empty() ? m.id : m.name);
        out += ",version=";
        append_escaped(out, m.version);
        out += ",description=";
        append_escaped(out, m.description);
        out += ",enabled=";
        out += m.enabled ? "true" : "false";
        out += ",config={";
        bool cf = true;
        for (auto& o : m.schema) {
            if (!cf) out += ",";
            cf = false;
            out += "{key=";
            append_escaped(out, o.key);
            out += ",type=";
            append_escaped(out, o.type);
            out += ",label=";
            append_escaped(out, o.label);
            out += ",value=";
            auto it = m.values.find(o.key);
            append_value(out, it != m.values.end() ? it->second : config_default(o));
            if (o.type == "int" || o.type == "float") {
                if (o.has_min) {
                    char b[32];
                    snprintf(b, sizeof b, ",min=%.6g", o.min_val);
                    out += b;
                }
                if (o.has_max) {
                    char b[32];
                    snprintf(b, sizeof b, ",max=%.6g", o.max_val);
                    out += b;
                }
                if (o.step != 0.0) {
                    char b[32];
                    snprintf(b, sizeof b, ",step=%.6g", o.step);
                    out += b;
                }
            }
            if (o.type == "enum") {
                out += ",options={";
                for (size_t k = 0; k < o.options.size(); ++k) {
                    if (k) out += ",";
                    append_escaped(out, o.options[k]);
                }
                out += "}";
            }
            out += "}";
        }
        out += "}}";
    }
    out += "}}";
    return out;
}

Result<std::string> apply_enabled_change(const std::string& mods_json, const std::string& id, bool enabled) {
    auto parsed_id = ModId::parse(id);
    if (!parsed_id.ok()) {
        return Result<std::string>::fail(with_operation(parsed_id.error(), "apply-enabled-change"));
    }
    ModState st;
    auto sf = parse_state(mods_json);
    if (!sf.ok()) {
        return Result<std::string>::fail(with_operation(sf.error(), "apply-enabled-change"));
    }
    st = sf.value();
    st.set(parsed_id.value(), enabled);
    return Result<std::string>::ok(st.serialize());
}

static bool strict_int(const std::string& s, long long& out) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= s.size()) return false;
    long long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

static bool strict_num(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* e = nullptr;
    out = strtod(s.c_str(), &e);
    return e && *e == '\0';
}

Result<std::string> apply_config_value(const std::vector<ConfigOption>& schema,
                                         const std::string& file_text, const std::string& key,
                                         const std::string& valstr) {
    auto fail = [&](const std::string& msg) {
        return Result<std::string>::fail(Error{"apply-config-value", key, errcat::kRange, msg});
    };
    const ConfigOption* opt = nullptr;
    for (auto& o : schema)
        if (o.key == key) {
            opt = &o;
            break;
        }
    if (!opt) return fail("unknown key");
    ConfigValue v;
    if (opt->type == "bool") {
        if (valstr == "1" || valstr == "true") v = ConfigValue::boolean(true);
        else if (valstr == "0" || valstr == "false") v = ConfigValue::boolean(false);
        else return fail("bad bool");
    } else if (opt->type == "int") {
        long long i = 0;
        if (!strict_int(valstr, i)) return fail("bad int");
        v = ConfigValue::integer(i);
    } else if (opt->type == "float") {
        double f = 0;
        if (!strict_num(valstr, f)) return fail("bad float");
        v = ConfigValue::number(f);
    } else if (opt->type == "string" || opt->type == "color") {
        // Empty string resets to default (documented UI contract).
        v = valstr.empty() ? config_default(*opt) : ConfigValue::text(valstr);
    } else if (opt->type == "enum") {
        v = ConfigValue::text(valstr);
    } else {
        return fail("bad type");
    }
    if (!config_validate(*opt, v)) return fail("invalid value");
    ConfigFile f;
    auto parsed = parse_config_file(file_text);
    if (parsed.ok()) f = parsed.value();
    std::map<std::string, ConfigValue> merged = f.values;
    merged[key] = v;
    return Result<std::string>::ok(serialize_config(schema, merged));
}

} // namespace ttmod
