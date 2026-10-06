#include "ttmod/manifest.hpp"
#include "ttmod/json.hpp"
#include "ttmod/validate.hpp"
#include "ttmod/version.hpp"

#include <climits>

namespace ttmod {
namespace {

bool req_str(const json& o, const char* key, std::string& out, std::string& error) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_string()) {
        error = std::string("bad ") + key;
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool req_int(const json& o, const char* key, int& out, std::string& error) {
    auto it = o.find(key);
    if (it == o.end() || !(it->is_number_integer() || it->is_number_unsigned())) {
        error = std::string("bad ") + key;
        return false;
    }
    long long v = it->get<long long>();
    if (v < INT_MIN || v > INT_MAX) {
        error = std::string("bad ") + key;
        return false;
    }
    out = (int)v;
    return true;
}

bool req_bool(const json& o, const char* key, bool& out, std::string& error) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_boolean()) {
        error = std::string("bad ") + key;
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool str_array(const json& o, const char* key, std::vector<std::string>& out, std::string& error) {
    auto it = o.find(key);
    if (it == o.end()) return true;
    if (!it->is_array()) {
        error = std::string("bad ") + key;
        return false;
    }
    for (auto& e : *it) {
        if (!e.is_string()) {
            error = std::string("bad ") + key;
            return false;
        }
        out.push_back(e.get<std::string>());
    }
    return true;
}

} // namespace

ModManifest parse_manifest(const std::string& text) {
    ModManifest m;
    if (text.size() > 1024 * 1024) {
        m.error = "manifest too large";
        return m;
    }
    json j;
    std::string perr;
    if (!parse_json_value(text, j, perr) || !j.is_object()) {
        m.error = perr.empty() ? "not an object" : perr;
        return m;
    }
    auto fail = [&](const std::string& e) {
        m.error = e;
        return m;
    };
    if (!req_str(j, "id", m.identity.id, m.error)) return fail(m.error);
    if (!is_valid_mod_id(m.identity.id)) return fail("bad id");
    auto vit = j.find("version");
    if (vit != j.end()) {
        if (!vit->is_string()) return fail("bad version");
        m.identity.version = vit->get<std::string>();
    }
    if (!req_int(j, "api", m.compat.api, m.error) || m.compat.api <= 0) return fail(m.error);
    if (j.contains("priority") && !req_int(j, "priority", m.overrides.priority, m.error)) return fail(m.error);
    if (j.contains("enabled") && !req_bool(j, "enabled", m.enabled, m.error)) return fail(m.error);
    if (j.contains("package_format")) {
        if (!req_int(j, "package_format", m.package_format, m.error)) return fail(m.error);
        if (m.package_format > kPackageFormat) return fail("unsupported package_format");
    }
    {
        auto it = j.find("arch");
        if (it != j.end()) {
            if (!it->is_string()) return fail("bad arch");
            m.compat.arch = parse_architecture(it->get<std::string>());
            if (m.compat.arch == Architecture::Unknown) return fail("bad arch");
        }
    }
    for (const char* k : {"name", "description", "plugin"}) {
        auto it = j.find(k);
        if (it != j.end()) {
            if (!it->is_string()) return fail(std::string("bad ") + k);
            if (k[0] == 'n') m.presentation.name = it->get<std::string>();
            else if (k[0] == 'd') m.presentation.description = it->get<std::string>();
            else m.plugin.path = it->get<std::string>();
        }
    }
    if (!str_array(j, "games", m.compat.games, m.error)) return fail(m.error);
    if (!str_array(j, "conflicts", m.deps.conflicts, m.error)) return fail(m.error);
    auto dit = j.find("depends");
    if (dit != j.end()) {
        if (!dit->is_array()) return fail("bad depends");
        for (auto& e : *dit) {
            if (!e.is_object()) return fail("bad depends");
            std::string did, dver;
            if (!req_str(e, "id", did, m.error)) return fail(m.error);
            auto vit2 = e.find("version");
            if (vit2 != e.end()) {
                if (!vit2->is_string()) return fail("bad depends");
                dver = vit2->get<std::string>();
            }
            m.deps.depends.emplace_back(did, dver);
        }
    }
    auto fit = j.find("files");
    if (fit != j.end()) {
        if (!fit->is_object()) return fail("bad files");
        for (auto& [from, to] : fit->items()) {
            if (!to.is_string()) return fail("bad files");
            m.overrides.files.emplace_back(from, to.get<std::string>());
        }
    }
    auto cit = j.find("config");
    if (cit != j.end()) {
        if (!cit->is_array()) return fail("bad config");
        std::string raw = cit->dump();
        std::string cerr;
        if (!parse_config_schema(raw, m.presentation.config, cerr)) return fail(std::string("bad config: ") + cerr);
    }
    // Unknown fields are skipped (additive optional fields only).
    m.ok = true;
    return m;
}

int compare_versions(const std::string& a, const std::string& b) {
    return Version(a).compare(Version(b));
}

} // namespace ttmod
