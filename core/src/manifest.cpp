// Manifest parsing in two layers (Step 4):
//   text -> JSON -> RawManifest   (syntax + JSON types only)
//   RawManifest -> ModManifest    (semantics: ids, paths, versions, ...)
// The split matters because the two fail differently: a syntax failure is
// about the bytes, a semantic failure is about the mod being wrong. Keeping
// them apart also means RawManifest can be inspected (CLI diagnostics,
// fuzzing) without any semantic policy applied.
#include "ttmod/manifest.hpp"
#include "ttmod/json.hpp"
#include "ttmod/validate.hpp"
#include "ttmod/version.hpp"

#include <climits>

namespace ttmod {
namespace {

// Field notes accumulate instead of returning early: a broken manifest
// should report every problem at once, not just the first.
struct Notes {
    std::vector<std::string> v;
    bool add(std::string s) {
        v.push_back(std::move(s));
        return false;
    }
};

const char* const kKnownFields[] = {"id",       "version",     "name",    "description",    "plugin", "arch",
                                    "api",      "priority",    "enabled", "package_format", "games",  "conflicts",
                                    "runtimes", "permissions", "depends", "files",          "config"};

bool known_field(const std::string& k) {
    for (auto* f : kKnownFields)
        if (k == f) return true;
    return false;
}

} // namespace

Result<RawManifest> read_raw_manifest(const std::string& text) {
    auto fail = [&](const std::string& e, const char* cat) {
        return Result<RawManifest>::fail(Error{"read-manifest", "", cat, e});
    };
    if (text.size() > 1024 * 1024) return fail("manifest too large", errcat::kLimit);
    auto parsed = parse_json_value(text);
    if (!parsed.ok())
        return Result<RawManifest>::fail(Error{"read-manifest", "", parsed.error().category, parsed.error().message});
    const json& j = parsed.value();
    if (!j.is_object()) return fail("not an object", errcat::kType);

    RawManifest raw;
    Notes notes;
    // Optional string: wrong type is a note, absent stays absent.
    auto opt_str = [&](const char* key, std::optional<std::string>& dst) {
        auto it = j.find(key);
        if (it == j.end()) return;
        if (!it->is_string()) {
            notes.add(std::string(key) + ": bad type");
            return;
        }
        dst = it->get<std::string>();
    };
    auto opt_int = [&](const char* key, std::optional<int>& dst) {
        auto it = j.find(key);
        if (it == j.end()) return;
        if (!(it->is_number_integer() || it->is_number_unsigned())) {
            notes.add(std::string(key) + ": bad type");
            return;
        }
        long long v = it->get<long long>(); // throws on overflow -> note
        if (v < INT_MIN || v > INT_MAX) {
            notes.add(std::string(key) + ": out of range");
            return;
        }
        dst = (int)v;
    };
    auto opt_bool = [&](const char* key, std::optional<bool>& dst) {
        auto it = j.find(key);
        if (it == j.end()) return;
        if (!it->is_boolean()) {
            notes.add(std::string(key) + ": bad type");
            return;
        }
        dst = it->get<bool>();
    };
    auto opt_str_array = [&](const char* key, std::optional<std::vector<std::string>>& dst) {
        auto it = j.find(key);
        if (it == j.end()) return;
        if (!it->is_array()) {
            notes.add(std::string(key) + ": bad type");
            return;
        }
        std::vector<std::string> out;
        for (auto& e : *it) {
            if (!e.is_string()) {
                notes.add(std::string(key) + ": bad element type");
                return;
            }
            out.push_back(e.get<std::string>());
        }
        dst = std::move(out);
    };

    opt_str("id", raw.id);
    opt_str("version", raw.version);
    opt_str("name", raw.name);
    opt_str("description", raw.description);
    opt_str("plugin", raw.plugin);
    opt_str("arch", raw.arch);
    opt_int("api", raw.api);
    opt_int("priority", raw.priority);
    opt_int("package_format", raw.package_format);
    opt_bool("enabled", raw.enabled);
    opt_str_array("games", raw.games);
    opt_str_array("conflicts", raw.conflicts);
    opt_str_array("runtimes", raw.runtimes);
    opt_str_array("permissions", raw.permissions);

    if (auto it = j.find("depends"); it != j.end()) {
        if (!it->is_array()) {
            notes.add("depends: bad type");
        } else {
            std::vector<RawManifest::RawDep> deps;
            for (auto& e : *it) {
                if (!e.is_object()) {
                    notes.add("depends: bad entry type");
                    continue;
                }
                RawManifest::RawDep d;
                auto idit = e.find("id");
                if (idit == e.end() || !idit->is_string()) {
                    notes.add("depends: missing id");
                    continue;
                }
                d.id = idit->get<std::string>();
                if (auto vit = e.find("version"); vit != e.end()) {
                    if (!vit->is_string()) {
                        notes.add("depends: bad version type");
                        continue;
                    }
                    d.version = vit->get<std::string>();
                }
                deps.push_back(std::move(d));
            }
            raw.depends = std::move(deps);
        }
    }

    if (auto it = j.find("files"); it != j.end()) {
        if (!it->is_object()) {
            notes.add("files: bad type");
        } else {
            std::vector<std::pair<std::string, std::string>> files;
            for (auto& [from, to] : it->items()) {
                if (!to.is_string()) {
                    notes.add("files[" + from + "]: bad value type");
                    continue;
                }
                files.emplace_back(from, to.get<std::string>());
            }
            raw.files = std::move(files);
        }
    }

    // Config keeps its JSON text: the schema layer owns its validation and
    // must not be run twice.
    if (auto it = j.find("config"); it != j.end()) {
        if (!it->is_array()) {
            notes.add("config: bad type");
        } else {
            raw.config_json = it->dump();
        }
    }

    // Unknown keys are recorded and ignored (additive forward compatibility).
    for (auto& [k, _] : j.items())
        if (!known_field(k)) raw.unknown_fields.push_back(k);

    if (!notes.v.empty())
        return Result<RawManifest>::fail(Error{
            "read-manifest", "", errcat::kType,
            notes.v.size() == 1 ? notes.v[0] : std::to_string(notes.v.size()) + " field problems: " + notes.v.front()});
    return Result<RawManifest>::ok(std::move(raw));
}

Result<ModManifest> validate_manifest(const RawManifest& raw) {
    ModManifest m;
    auto fail = [&](const std::string& e, const std::string& cat = "manifest") {
        return Result<ModManifest>::fail(Error{"validate-manifest", m.identity.id.str(), cat, e});
    };

    // Required: id + api.
    if (!raw.id) return fail("missing id", errcat::kMissing);
    auto mid = ModId::parse(*raw.id);
    if (!mid.ok()) return fail("bad id", errcat::kType);
    m.identity.id = mid.value();

    if (raw.version) {
        auto ver = Version::parse(*raw.version);
        if (!ver.ok()) return fail(std::string("bad version: ") + ver.error().message, errcat::kSyntax);
        m.identity.version = ver.value().str();
    }

    if (!raw.api) return fail("missing api", errcat::kMissing);
    if (*raw.api <= 0) return fail("api must be positive", errcat::kRange);
    m.compat.api = *raw.api;

    if (raw.priority) m.overrides.priority = *raw.priority;
    if (raw.enabled) m.enabled = *raw.enabled;

    if (raw.package_format) {
        // A newer writer must not be silently read by an older reader.
        if (*raw.package_format > kPackageFormat) return fail("unsupported package_format", errcat::kRange);
        if (*raw.package_format < 1) return fail("bad package_format", errcat::kRange);
        m.package_format = *raw.package_format;
    }

    if (raw.arch) {
        m.compat.arch = parse_architecture(*raw.arch);
        if (m.compat.arch == Architecture::Unknown) return fail("bad arch", errcat::kType);
    }

    if (raw.name) m.presentation.name = *raw.name;
    if (raw.description) m.presentation.description = *raw.description;
    if (raw.plugin) {
        // Plugin paths cross into the filesystem: the one canonical validator.
        if (!raw.plugin->empty()) {
            auto p = validate_mod_relative_path(*raw.plugin);
            if (!p.ok()) return fail(std::string("bad plugin: ") + p.error().message, p.error().category);
        }
        m.plugin.path = *raw.plugin;
    }

    if (raw.games) m.compat.games = *raw.games;

    if (raw.conflicts) {
        for (auto& c : *raw.conflicts) {
            auto cid = ModId::parse(c);
            if (!cid.ok()) return fail("bad conflicts: " + c, errcat::kType);
            m.deps.conflicts.push_back(cid.value());
        }
    }

    if (raw.runtimes) {
        for (auto& r : *raw.runtimes) {
            auto rt = parse_runtime(r);
            // Unknown runtime names are rejected: a typo would otherwise
            // silently produce a mod that declares no runtime at all.
            if (!rt) return fail("bad runtimes: " + r, errcat::kType);
            m.runtime.runtimes.push_back(*rt);
        }
    }

    if (raw.permissions) {
        for (auto& p : *raw.permissions) {
            auto pp = parse_permission(p);
            // Permissions are a security boundary, not a hint.
            if (!pp) return fail("bad permissions: " + p, errcat::kType);
            m.runtime.permissions.push_back(*pp);
        }
    }

    if (raw.depends) {
        for (auto& d : *raw.depends) {
            auto dep = ModId::parse(d.id);
            if (!dep.ok()) return fail("bad depends: " + d.id, errcat::kType);
            if (!d.version.empty()) {
                auto vc = VersionConstraint::parse(d.version);
                if (!vc.ok()) return fail("bad depends version: " + vc.error().message, errcat::kSyntax);
            }
            m.deps.depends.emplace_back(dep.value(), d.version);
        }
    }

    if (raw.files) {
        for (auto& [from, to] : *raw.files) {
            // Both sides cross into filesystem paths. The replacement must be
            // a safe mod-relative subpath; the game path is a game-side key
            // (root-relative or absolute) validated by the resolver, not here.
            auto rp = validate_mod_relative_path(to);
            if (!rp.ok()) return fail("bad files[" + from + "]: " + rp.error().message, rp.error().category);
            m.overrides.files.emplace_back(from, to);
        }
    }

    if (raw.config_json) {
        auto schema = parse_config_schema(*raw.config_json);
        if (!schema.ok()) return fail(std::string("bad config: ") + schema.error().message, errcat::kType);
        m.presentation.config = schema.value();
    }

    return Result<ModManifest>::ok(std::move(m));
}

Result<ModManifest> parse_manifest(const std::string& text) {
    auto raw = read_raw_manifest(text);
    if (!raw.ok()) {
        // Keep the operation name callers already log against.
        return Result<ModManifest>::fail(Error{"parse-manifest", "", raw.error().category, raw.error().message});
    }
    return validate_manifest(raw.value());
}

int compare_versions(const std::string& a, const std::string& b) {
    // Malformed side sorts as 0.0.0 rather than throwing or aborting: this
    // helper only exists for loose display callers. Validated paths parse
    // versions with Version::parse and get a real Error instead.
    auto va = Version::parse(a);
    auto vb = Version::parse(b);
    const Version zero = Version::parse("").value();
    return (va.ok() ? va.value() : zero).compare(vb.ok() ? vb.value() : zero);
}

} // namespace ttmod