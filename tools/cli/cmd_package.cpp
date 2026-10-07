#include "commands.hpp"

#include <cstdio>
#include <string>

#include "ttmod/manifest.hpp"
#include "ttmod/package.hpp"

static void print_packinfo(const std::string& path) {
    auto insp = ttmod::inspect_package(path);
    if (!insp.ok()) {
        std::printf("INVALID: %s\n", insp.error().message.c_str());
        return;
    }
    auto pm = ttmod::parse_manifest(insp.value().manifest_text);
    if (!pm.ok()) {
        std::printf("INVALID: %s\n", pm.error().message.c_str());
        return;
    }
    auto m = pm.value();
    const auto& files = insp.value().files;
    std::printf("id: %s\nname: %s\nversion: %s\napi: %d\npackage_format: %d\ngames:", m.identity.id.str().c_str(),
                "(see manifest)", m.identity.version.str().c_str(), m.compat.api, m.package_format);
    for (auto& g : m.compat.games) std::printf(" %s", g.c_str());
    std::printf("\npriority: %d\nenabled-field: %s\nfiles: %u\nplugin: %s\narch: %s\nentries: %u\n",
                m.overrides.priority, m.enabled ? "true" : "false", (unsigned)m.overrides.files.size(),
                m.plugin.path.empty() ? "(none)" : m.plugin.path.c_str(),
                ttmod::to_string(m.compat.arch), (unsigned)files.size());
    bool native = !m.plugin.path.empty();
    for (auto& f : files)
        if (f.name.size() > 4 && f.name.compare(f.name.size() - 4, 4, ".dll") == 0) native = true;
    std::printf("contains native code: %s\n", native ? "YES - can execute arbitrary code" : "no");
    if (!m.deps.depends.empty()) {
        std::printf("depends:");
        for (auto& [id, ver] : m.deps.depends)
            std::printf(" %s%s%s", id.str().c_str(), ver.empty() ? "" : " ", ver.str().c_str());
        std::printf("\n");
    }
    if (!m.deps.conflicts.empty()) {
        std::printf("conflicts:");
        for (auto& c : m.deps.conflicts) std::printf(" %s", c.str().c_str());
        std::printf("\n");
    }
    if (!m.runtime.runtimes.empty()) {
        std::printf("runtimes:");
        for (auto& r : m.runtime.runtimes) std::printf(" %s", ttmod::to_string(r));
        std::printf("\n");
    }
    if (!m.runtime.permissions.empty()) {
        std::printf("permissions:");
        for (auto& r : m.runtime.permissions) std::printf(" %s", ttmod::to_string(r));
        std::printf("\n");
    }
    for (auto& f : files)
        std::printf("  %s (%llu)\n", f.name.c_str(), (unsigned long long)f.size);
}

int cmd_package(int argc, char** argv) {
    if (argc < 1) return cmd_usage();
    std::string sub = argv[0];
    if (sub == "create" && argc == 3) {
        auto created = ttmod::create_package(argv[1], argv[2]);
        if (!created.ok()) {
            std::printf("create failed: %s\n", created.error().message.c_str());
            return 1;
        }
        std::printf("created %s\n", argv[2]);
        return 0;
    }
    if (sub == "validate" && argc == 2) {
        auto insp = ttmod::inspect_package(argv[1]);
        if (!insp.ok()) {
            std::printf("INVALID: %s\n", insp.error().message.c_str());
            return 1;
        }
        auto pm = ttmod::parse_manifest(insp.value().manifest_text);
        if (!pm.ok()) {
            std::printf("INVALID: %s\n", pm.error().message.c_str());
            return 1;
        }
        auto m = pm.value();
        std::printf("valid: %s %s (%u files)\n", m.identity.id.str().c_str(), m.identity.version.str().c_str(),
                    (unsigned)insp.value().files.size());
        return 0;
    }
    if (sub == "info" && argc == 2) {
        print_packinfo(argv[1]);
        return 0;
    }
    return cmd_usage();
}
