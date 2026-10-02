#include "ttmod/manifest.hpp"
#include <cctype>

namespace ttmod {
namespace {

struct P {
    const char* s;
    const char* end;
    std::string err;
    void ws() { while (s < end && isspace((unsigned char)*s)) ++s; }
    bool lit(char c) { ws(); if (s < end && *s == c) { ++s; return true; } return false; }
    bool str(std::string& out) {
        ws();
        if (s >= end || *s != '"') { err = "expected string"; return false; }
        ++s;
        out.clear();
        while (s < end && *s != '"') {
            if (*s == '\\' && s + 1 < end) { ++s; out += *s++; }
            else out += *s++;
        }
        if (s >= end) { err = "unterminated string"; return false; }
        ++s;
        return true;
    }
    bool integer(int& out) {
        ws();
        bool neg = false;
        if (s < end && *s == '-') { neg = true; ++s; }
        if (s >= end || !isdigit((unsigned char)*s)) { err = "expected int"; return false; }
        long v = 0;
        while (s < end && isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
        out = neg ? -(int)v : (int)v;
        return true;
    }
    bool boolean(bool& out) {
        ws();
        if (s + 4 <= end && std::string(s, s + 4) == "true") { s += 4; out = true; return true; }
        if (s + 5 <= end && std::string(s, s + 5) == "false") { s += 5; out = false; return true; }
        err = "expected bool";
        return false;
    }
    bool skipval() { // skip one JSON value (flat: string/int/array/object, 1 level)
        ws();
        if (s >= end) return false;
        if (*s == '"') { std::string t; return str(t); }
        if (*s == '[') {
            ++s;
            int d = 1;
            bool instr = false;
            while (s < end && d) {
                if (instr) { if (*s == '\\') ++s; else if (*s == '"') instr = false; }
                else if (*s == '"') instr = true;
                else if (*s == '[') ++d;
                else if (*s == ']') --d;
                ++s;
            }
            return d == 0;
        }
        if (*s == '{') {
            ++s;
            int d = 1;
            bool instr = false;
            while (s < end && d) {
                if (instr) { if (*s == '\\') ++s; else if (*s == '"') instr = false; }
                else if (*s == '"') instr = true;
                else if (*s == '{') ++d;
                else if (*s == '}') --d;
                ++s;
            }
            return d == 0;
        }
        while (s < end && *s != ',' && *s != '}' && *s != ']') ++s;
        return true;
    }
};

} // namespace

ModManifest parse_manifest(const std::string& text) {
    ModManifest m;
    P p{text.data(), text.data() + text.size()};
    if (!p.lit('{')) { m.error = "not an object"; return m; }
    bool first = true;
    while (true) {
        p.ws();
        if (p.s < p.end && *p.s == '}') { ++p.s; break; }
        if (!first && !p.lit(',')) { m.error = "expected ,"; return m; }
        first = false;
        std::string key;
        if (!p.str(key)) { m.error = p.err; return m; }
        if (!p.lit(':')) { m.error = "expected :"; return m; }
        if (key == "id") { if (!p.str(m.id)) { m.error = p.err; return m; } }
        else if (key == "version") { if (!p.str(m.version)) { m.error = p.err; return m; } }
        else if (key == "name") { if (!p.str(m.name)) { m.error = p.err; return m; } }
        else if (key == "description") {
            if (!p.str(m.description)) { m.error = p.err; return m; }
        } else if (key == "config") {
            // capture the raw array span, then delegate to the schema parser
            p.ws();
            if (p.s >= p.end || *p.s != '[') { m.error = "config not array"; return m; }
            const char* b = p.s;
            int d = 0;
            bool instr = false;
            while (p.s < p.end) {
                char c = *p.s++;
                if (instr) {
                    if (c == '\\') { if (p.s < p.end) ++p.s; }
                    else if (c == '"') instr = false;
                } else if (c == '"') {
                    instr = true;
                } else if (c == '[') {
                    ++d;
                } else if (c == ']') {
                    if (--d == 0) break;
                }
            }
            if (d != 0) { m.error = "config unterminated"; return m; }
            std::string raw(b, p.s);
            std::string cerr;
            if (!parse_config_schema(raw, m.config, cerr)) {
                m.error = std::string("bad config: ") + cerr;
                return m;
            }
        }
        else if (key == "api") { if (!p.integer(m.api)) { m.error = p.err; return m; } }
        else if (key == "priority") { if (!p.integer(m.priority)) { m.error = p.err; return m; } }
        else if (key == "plugin") { if (!p.str(m.plugin)) { m.error = p.err; return m; } }
        else if (key == "arch") { if (!p.str(m.arch)) { m.error = p.err; return m; } }
        else if (key == "package_format") {
            if (!p.integer(m.package_format)) { m.error = p.err; return m; }
            if (m.package_format > kPackageFormat) { m.error = "unsupported package_format"; return m; }
        }
        else if (key == "enabled") { if (!p.boolean(m.enabled)) { m.error = p.err; return m; } }
        else if (key == "conflicts") {
            if (!p.lit('[')) { m.error = "conflicts not array"; return m; }
            p.ws();
            if (p.s < p.end && *p.s != ']') {
                while (true) {
                    std::string g;
                    if (!p.str(g)) { m.error = p.err; return m; }
                    m.conflicts.push_back(g);
                    p.ws();
                    if (p.lit(',')) continue;
                    break;
                }
            }
            if (!p.lit(']')) { m.error = "conflicts unterminated"; return m; }
        } else if (key == "depends") {
            if (!p.lit('[')) { m.error = "depends not array"; return m; }
            p.ws();
            if (p.s < p.end && *p.s != ']') {
                while (true) {
                    if (!p.lit('{')) { m.error = "depends entry not object"; return m; }
                    std::string did, dver;
                    while (true) {
                        p.ws();
                        if (p.s < p.end && *p.s == '}') { ++p.s; break; }
                        std::string dk;
                        if (!p.str(dk)) { m.error = p.err; return m; }
                        if (!p.lit(':')) { m.error = "expected :"; return m; }
                        if (dk == "id") { if (!p.str(did)) { m.error = p.err; return m; } }
                        else if (dk == "version") { if (!p.str(dver)) { m.error = p.err; return m; } }
                        else if (!p.skipval()) { m.error = "bad value"; return m; }
                        p.ws();
                        if (p.lit(',')) continue;
                        if (p.s < p.end && *p.s == '}') { ++p.s; break; }
                        m.error = "depends entry unterminated";
                        return m;
                    }
                    if (did.empty()) { m.error = "depends entry missing id"; return m; }
                    m.depends.emplace_back(did, dver);
                    p.ws();
                    if (p.lit(',')) continue;
                    break;
                }
            }
            if (!p.lit(']')) { m.error = "depends unterminated"; return m; }
        } else if (key == "files") {
            if (!p.lit('{')) { m.error = "files not object"; return m; }
            p.ws();
            if (p.s < p.end && *p.s != '}') {
                while (true) {
                    std::string from, to;
                    if (!p.str(from)) { m.error = p.err; return m; }
                    if (!p.lit(':')) { m.error = "expected :"; return m; }
                    if (!p.str(to)) { m.error = p.err; return m; }
                    m.files.emplace_back(from, to);
                    p.ws();
                    if (p.lit(',')) continue;
                    break;
                }
            }
            if (!p.lit('}')) { m.error = "files unterminated"; return m; }
        }
        else if (key == "games") {
            if (!p.lit('[')) { m.error = "games not array"; return m; }
            p.ws();
            if (p.s < p.end && *p.s != ']') {
                while (true) {
                    std::string g;
                    if (!p.str(g)) { m.error = p.err; return m; }
                    m.games.push_back(g);
                    p.ws();
                    if (p.lit(',')) continue;
                    break;
                }
            }
            if (!p.lit(']')) { m.error = "games unterminated"; return m; }
        } else if (!p.skipval()) { m.error = "bad value"; return m; }
    }
    if (m.id.empty()) { m.error = "missing id"; return m; }
    if (m.api <= 0) { m.error = "missing api"; return m; }
    m.ok = true;
    return m;
}

static long num_prefix(const std::string& s) {
    long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') break;
        v = v * 10 + (c - '0');
    }
    return v;
}

int compare_versions(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        size_t i2 = a.find('.', i), j2 = b.find('.', j);
        std::string pa = a.substr(i, i2 == std::string::npos ? i2 : i2 - i);
        std::string pb = b.substr(j, j2 == std::string::npos ? j2 : j2 - j);
        long va = num_prefix(pa), vb = num_prefix(pb);
        if (va != vb) return va < vb ? -1 : 1;
        // same numeric prefix: a part with extra non-numeric tail sorts equal
        // (ignored for gating); continue.
        i = i2 == std::string::npos ? a.size() : i2 + 1;
        j = j2 == std::string::npos ? b.size() : j2 + 1;
    }
    return 0;
}

} // namespace ttmod
