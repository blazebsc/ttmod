// Per-mod configuration backend (see header).
#include "ttmod/modconfig.hpp"
#include "ttmod/modstate.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ttmod {
namespace {

struct P {
    const char* s;
    const char* end;
    std::string err;
    void ws() {
        while (s < end && isspace((unsigned char)*s)) ++s;
    }
    bool lit(char c) {
        ws();
        if (s < end && *s == c) {
            ++s;
            return true;
        }
        return false;
    }
    bool str(std::string& out) {
        ws();
        if (s >= end || *s != '"') {
            err = "expected string";
            return false;
        }
        ++s;
        out.clear();
        while (s < end && *s != '"') {
            if (*s == '\\' && s + 1 < end) {
                ++s;
                char e = *s++;
                // keep it simple: JSON escapes resolve to the char itself,
                // except n/r/t which become real control chars.
                if (e == 'n') out += '\n';
                else if (e == 'r') out += '\r';
                else if (e == 't') out += '\t';
                else out += e;
            } else {
                out += *s++;
            }
        }
        if (s >= end) {
            err = "unterminated string";
            return false;
        }
        ++s;
        return true;
    }
    // integer or float; reports which.
    bool number(double& out, bool& is_int) {
        ws();
        const char* b = s;
        bool neg = false;
        if (s < end && *s == '-') {
            neg = true;
            ++s;
        }
        if (s >= end || !isdigit((unsigned char)*s)) {
            err = "expected number";
            return false;
        }
        double v = 0;
        while (s < end && isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
        is_int = true;
        if (s < end && *s == '.') {
            is_int = false;
            ++s;
            double f = 0.1;
            if (s >= end || !isdigit((unsigned char)*s)) {
                err = "bad fraction";
                return false;
            }
            while (s < end && isdigit((unsigned char)*s)) {
                v += (*s++ - '0') * f;
                f *= 0.1;
            }
        }
        if (s < end && (*s == 'e' || *s == 'E')) {
            is_int = false;
            ++s;
            bool eneg = false;
            if (s < end && (*s == '-' || *s == '+')) eneg = *s++ == '-';
            int e = 0;
            if (s >= end || !isdigit((unsigned char)*s)) {
                err = "bad exponent";
                return false;
            }
            while (s < end && isdigit((unsigned char)*s)) e = e * 10 + (*s++ - '0');
            double m = 1;
            for (int i = 0; i < e; ++i) m *= 10;
            v = eneg ? v / m : v * m;
        }
        (void)b;
        out = neg ? -v : v;
        return true;
    }
    bool boolean(bool& out) {
        ws();
        if (s + 4 <= end && std::string(s, s + 4) == "true") {
            s += 4;
            out = true;
            return true;
        }
        if (s + 5 <= end && std::string(s, s + 5) == "false") {
            s += 5;
            out = false;
            return true;
        }
        err = "expected bool";
        return false;
    }
    bool skipval() {
        ws();
        if (s >= end) return false;
        if (*s == '"') {
            std::string t;
            return str(t);
        }
        if (*s == '[' || *s == '{') {
            char o = *s++, c = (o == '[') ? ']' : '}';
            int d = 1;
            bool instr = false;
            while (s < end && d) {
                if (instr) {
                    if (*s == '\\') ++s;
                    else if (*s == '"') instr = false;
                } else if (*s == '"') {
                    instr = true;
                } else if (*s == o) {
                    ++d;
                } else if (*s == c) {
                    --d;
                }
                ++s;
            }
            return d == 0;
        }
        while (s < end && *s != ',' && *s != '}' && *s != ']') ++s;
        return true;
    }
};

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

bool parse_config_schema(const std::string& json, std::vector<ConfigOption>& out,
                         std::string& error) {
    P p{json.data(), json.data() + json.size()};
    if (!p.lit('[')) {
        error = "config not array";
        return false;
    }
    p.ws();
    if (p.s < p.end && *p.s != ']') {
        while (true) {
            if (!p.lit('{')) {
                error = "config entry not object";
                return false;
            }
            ConfigOption o;
            bool have_key = false, have_type = false, have_label = false;
            bool have_default = false;
            std::string def_s;
            double def_n = 0;
            bool def_n_int = true, def_b = false;
            int def_kind = 0; // 0 none, 1 bool, 2 num, 3 str
            while (true) {
                p.ws();
                if (p.s < p.end && *p.s == '}') {
                    ++p.s;
                    break;
                }
                std::string k;
                if (!p.str(k)) {
                    error = p.err;
                    return false;
                }
                if (!p.lit(':')) {
                    error = "expected :";
                    return false;
                }
                if (k == "key") {
                    if (!p.str(o.key) || o.key.empty()) {
                        error = "bad key";
                        return false;
                    }
                    have_key = true;
                } else if (k == "type") {
                    if (!p.str(o.type) || !valid_type(o.type)) {
                        error = "bad type";
                        return false;
                    }
                    have_type = true;
                } else if (k == "label") {
                    if (!p.str(o.label) || o.label.empty()) {
                        error = "bad label";
                        return false;
                    }
                    have_label = true;
                } else if (k == "default") {
                    p.ws();
                    if (p.s < p.end && *p.s == '"') {
                        if (!p.str(def_s)) {
                            error = p.err;
                            return false;
                        }
                        def_kind = 3;
                    } else if (p.s + 4 <= p.end &&
                               (std::string(p.s, p.s + 4) == "true" ||
                                std::string(p.s, p.s + 5) == "false")) {
                        if (!p.boolean(def_b)) {
                            error = p.err;
                            return false;
                        }
                        def_kind = 1;
                    } else {
                        if (!p.number(def_n, def_n_int)) {
                            error = p.err;
                            return false;
                        }
                        def_kind = 2;
                    }
                    have_default = true;
                } else if (k == "min") {
                    if (!p.number(o.min_val, def_n_int)) {
                        error = p.err;
                        return false;
                    }
                    o.has_min = true;
                } else if (k == "max") {
                    if (!p.number(o.max_val, def_n_int)) {
                        error = p.err;
                        return false;
                    }
                    o.has_max = true;
                } else if (k == "step") {
                    if (!p.number(o.step, def_n_int)) {
                        error = p.err;
                        return false;
                    }
                } else if (k == "options") {
                    if (!p.lit('[')) {
                        error = "options not array";
                        return false;
                    }
                    p.ws();
                    if (p.s < p.end && *p.s != ']') {
                        while (true) {
                            std::string v;
                            if (!p.str(v)) {
                                error = p.err;
                                return false;
                            }
                            o.options.push_back(v);
                            p.ws();
                            if (p.lit(',')) continue;
                            break;
                        }
                    }
                    if (!p.lit(']')) {
                        error = "options unterminated";
                        return false;
                    }
                } else if (!p.skipval()) {
                    error = "bad value";
                    return false;
                }
                p.ws();
                if (p.lit(',')) continue;
                // loop re-checks for '}'
            }
            if (!have_key || !have_type || !have_label) {
                error = "entry missing key/type/label";
                return false;
            }
            if (o.type == "enum" && o.options.empty()) {
                error = "enum needs options";
                return false;
            }
            if (o.type == "color" && have_default && !valid_color(def_s)) {
                error = "color default must be #RRGGBB";
                return false;
            }
            if (have_default) {
                if (def_kind == 1) o.def_bool = def_b;
                if (def_kind == 2) {
                    o.def_int = (long long)def_n;
                    o.def_float = def_n;
                }
                if (def_kind == 3) o.def_str = def_s;
            } else if (o.type == "enum") {
                o.def_str = o.options[0];
            }
            out.push_back(o);
            p.ws();
            if (p.lit(',')) continue;
            break;
        }
    }
    if (!p.lit(']')) {
        error = "config unterminated";
        return false;
    }
    return true;
}

ConfigFile parse_config_file(const std::string& text) {
    ConfigFile f;
    P p{text.data(), text.data() + text.size()};
    p.ws();
    if (p.s >= p.end) return f; // blank -> empty (all defaults)
    if (!p.lit('{')) {
        f.ok = false;
        return f;
    }
    while (true) {
        p.ws();
        if (p.s < p.end && *p.s == '}') {
            ++p.s;
            break;
        }
        std::string k;
        if (!p.str(k)) {
            f.ok = false;
            return f;
        }
        if (!p.lit(':')) {
            f.ok = false;
            return f;
        }
        p.ws();
        if (p.s < p.end && *p.s == '"') {
            std::string v;
            if (!p.str(v)) {
                f.ok = false;
                return f;
            }
            f.values[k] = ConfigValue::text(v);
        } else if (p.s + 4 <= p.end &&
                   (std::string(p.s, p.s + 4) == "true" || std::string(p.s, p.s + 5) == "false")) {
            bool b = false;
            if (!p.boolean(b)) {
                f.ok = false;
                return f;
            }
            f.values[k] = ConfigValue::boolean(b);
        } else {
            double v = 0;
            bool is_int = true;
            if (!p.number(v, is_int)) {
                f.ok = false;
                return f;
            }
            f.values[k] = is_int ? ConfigValue::integer((long long)v) : ConfigValue::number(v);
        }
        p.ws();
        if (p.lit(',')) continue;
        p.ws();
        if (p.s < p.end && *p.s == '}') {
            ++p.s;
            break;
        }
        f.ok = false;
        return f;
    }
    return f;
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
    if (!file.ok) {
        for (auto& o : schema) out[o.key] = config_default(o);
        return out;
    }
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

std::string apply_enabled_change(const std::string& mods_json, const std::string& id,
                                 bool enabled) {
    ModState st;
    StateFile sf = parse_state(mods_json);
    if (sf.ok) st = sf.state;
    st.set(id, enabled);
    return st.serialize();
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

SetValueResult apply_config_value(const std::vector<ConfigOption>& schema,
                                  const std::string& file_text, const std::string& key,
                                  const std::string& valstr) {
    SetValueResult r;
    const ConfigOption* opt = nullptr;
    for (auto& o : schema)
        if (o.key == key) {
            opt = &o;
            break;
        }
    if (!opt) return r;
    ConfigValue v;
    if (opt->type == "bool") {
        if (valstr == "1" || valstr == "true") v = ConfigValue::boolean(true);
        else if (valstr == "0" || valstr == "false") v = ConfigValue::boolean(false);
        else return r;
    } else if (opt->type == "int") {
        long long i = 0;
        if (!strict_int(valstr, i)) return r;
        v = ConfigValue::integer(i);
    } else if (opt->type == "float") {
        double f = 0;
        if (!strict_num(valstr, f)) return r;
        v = ConfigValue::number(f);
    } else if (opt->type == "string" || opt->type == "color") {
        // Empty string resets to default (documented UI contract).
        v = valstr.empty() ? config_default(*opt) : ConfigValue::text(valstr);
    } else if (opt->type == "enum") {
        v = ConfigValue::text(valstr);
    } else {
        return r;
    }
    if (!config_validate(*opt, v)) return r;
    ConfigFile f = parse_config_file(file_text);
    std::map<std::string, ConfigValue> merged;
    if (f.ok) merged = f.values;
    merged[key] = v;
    r.ok = true;
    r.file_text = serialize_config(schema, merged);
    return r;
}

} // namespace ttmod
