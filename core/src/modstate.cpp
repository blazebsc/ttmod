#include "ttmod/modstate.hpp"
#include "ttmod/manifest.hpp"
#include <cctype>

namespace ttmod {

bool ModState::enabled_for(const std::string& id, bool manifest_default) const {
    auto it = overrides.find(id);
    return it == overrides.end() ? manifest_default : it->second;
}

bool effective_enabled(const ModManifest& manifest, const ModState& state) {
    return state.enabled_for(manifest.id, manifest.enabled);
}

void ModState::set(const std::string& id, bool enabled) {
    overrides[id] = enabled;
}

std::string ModState::serialize() const {
    std::string o = "{\n";
    bool first = true;
    for (auto& [id, en] : overrides) {
        if (!first) o += ",\n";
        first = false;
        o += "    \"" + id + "\": {\"enabled\": " + (en ? "true" : "false") + "}";
    }
    o += first ? "}" : "\n}";
    return o;
}

namespace {

struct P {
    const char* s;
    const char* end;
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
        if (s >= end || *s != '"') return false;
        ++s;
        out.clear();
        while (s < end && *s != '"') {
            if (*s == '\\' && s + 1 < end) {
                ++s;
                out += *s++;
            } else
                out += *s++;
        }
        if (s >= end) return false;
        ++s;
        return true;
    }
    // skip nested value (object/array/string/other)
    bool skip() {
        ws();
        if (s >= end) return false;
        if (*s == '"') {
            std::string t;
            return str(t);
        }
        if (*s == '{' || *s == '[') {
            char o = *s, c = o == '{' ? '}' : ']';
            ++s;
            int d = 1;
            bool instr = false;
            while (s < end && d) {
                if (instr) {
                    if (*s == '\\') ++s;
                    else if (*s == '"') instr = false;
                } else if (*s == '"')
                    instr = true;
                else if (*s == o)
                    ++d;
                else if (*s == c)
                    --d;
                ++s;
            }
            return d == 0;
        }
        while (s < end && *s != ',' && *s != '}' && *s != ']') ++s;
        return true;
    }
};

} // namespace

StateFile parse_state(const std::string& text) {
    StateFile f;
    P p{text.data(), text.data() + text.size()};
    p.ws();
    if (!p.lit('{')) {
        // Empty/missing file content is not fatal: treat as empty state.
        bool blank = true;
        for (char c : text)
            if (!isspace((unsigned char)c)) blank = false;
        if (blank) return f;
        f.ok = false;
        f.error = "not an object";
        return f;
    }
    bool first = true;
    while (true) {
        p.ws();
        if (p.s < p.end && *p.s == '}') break;
        if (!first && !p.lit(',')) {
            f.ok = false;
            f.error = "expected ,";
            return f;
        }
        first = false;
        std::string id;
        if (!p.str(id) || !p.lit(':') || !p.lit('{')) {
            f.ok = false;
            f.error = "bad entry";
            return f;
        }
        bool enabled = true, seen = false;
        while (true) {
            p.ws();
            if (p.s < p.end && *p.s == '}') {
                ++p.s;
                break;
            }
            std::string k;
            if (!p.str(k) || !p.lit(':')) {
                f.ok = false;
                f.error = "bad field";
                return f;
            }
            if (k == "enabled") {
                p.ws();
                if (p.s + 4 <= p.end && std::string(p.s, p.s + 4) == "true") {
                    p.s += 4;
                    enabled = true;
                    seen = true;
                } else if (p.s + 5 <= p.end && std::string(p.s, p.s + 5) == "false") {
                    p.s += 5;
                    enabled = false;
                    seen = true;
                } else {
                    f.ok = false;
                    f.error = "enabled not bool";
                    return f;
                }
            } else if (!p.skip()) {
                f.ok = false;
                f.error = "bad value";
                return f;
            }
            p.ws();
            if (p.lit(',')) continue;
            if (p.s < p.end && *p.s == '}') {
                ++p.s;
                break;
            }
            f.ok = false;
            f.error = "entry unterminated";
            return f;
        }
        if (seen) f.state.overrides[id] = enabled;
    }
    return f;
}

} // namespace ttmod
