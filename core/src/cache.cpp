// Package cache sync. See cache.hpp.
#include "ttmod/cache.hpp"
#include "ttmod/detect.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/package.hpp"

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string_view>

namespace ttmod {
namespace {

namespace fs = std::filesystem;

std::string read_file(const std::string& p) {
    FILE* f = ttmod::file_io::open_read(p);
    if (!f) return "";
    std::string s;
    char b[4096];
    size_t r;
    while ((r = fread(b, 1, sizeof b, f)) > 0) s.append(b, r);
    fclose(f);
    return s;
}

std::string marker_for(const std::string& package_path) {
    // Identity: content hash + size + cache schema + package format. Never
    // mtime (unreliable). Old size+mtime markers never match -> one clean
    // re-extract.
    //
    // NOT a security or authentication property: FNV-1a is a fast
    // non-cryptographic hash chosen because it needs no dependency. It
    // detects "did this file change", not "is this file trustworthy".
    // Package authenticity is not a goal (see SECURITY.md).
    auto digest = fnv1a_file(package_path);
    if (!digest.ok()) return "";
    char m[128];
    snprintf(m, sizeof m, "v=%d fmt=%d fnv=%016llx size=%llu", kCacheSchema, kPackageFormat,
             (unsigned long long)digest.value().fnv1a64, (unsigned long long)digest.value().size);
    return m;
}

// Suffixes for in-flight and retired copies. Recognizable as ours so a
// leftover from a killed process can be identified and swept. string_view
// (not constexpr const char* + sizeof: that measures the pointer).
constexpr std::string_view kRetiredSuffix = ".ttmod-old";
constexpr std::string_view kStagingSuffix = ".ttmod-new";

std::string staging_dir(const std::string& dir) {
    return dir + std::string(kStagingSuffix);
}
std::string retired_dir(const std::string& dir) {
    return dir + std::string(kRetiredSuffix);
}

bool has_retired_suffix(const std::string& name) {
    return name.size() > kRetiredSuffix.size() && name.ends_with(kRetiredSuffix);
}

// Parsed id for a retired cache entry dir, or no value when the name is not ours.
std::optional<ModId> entry_id(const std::string& name) {
    if (!has_retired_suffix(name)) return std::nullopt;
    auto id = ModId::parse(name.substr(0, name.size() - kRetiredSuffix.size()));
    if (!id.ok()) return std::nullopt;
    return id.value();
}

} // namespace

Result<CacheSync> sync_package_cache(const std::string& cache_dir,
                                     const std::vector<std::pair<ModId, std::string>>& packaged) {
    CacheSync out;
    std::error_code ec;
    fs::create_directories(cache_dir, ec);
    if (ec) {
        return Result<CacheSync>::fail(Error{"sync-cache", cache_dir, errcat::kIO, "cannot create cache dir"});
    }
    // Recovery sweep: a crash between "move live aside" and "move new in"
    // leaves <id>.ttmod-old holding the only good copy. Restore it before
    // anything else, then clear staging junk.
    for (auto& e : fs::directory_iterator(cache_dir, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        std::string name = e.path().filename().string();
        auto id = entry_id(name);
        if (id) {
            std::string live = (fs::path(cache_dir) / id->str()).string();
            if (!fs::exists(live, ec)) {
                fs::rename(e.path(), live, ec); // restore the good copy
                out.log.push_back(id->str() + ": cache restored after crash");
            } else {
                fs::remove_all(e.path(), ec); // live copy is fine; old is junk
            }
        } else if (name.size() > kStagingSuffix.size() && name.ends_with(kStagingSuffix)) {
            fs::remove_all(e.path(), ec); // partial extraction, never trusted
        }
    }

    std::map<ModId, bool> wanted;
    for (auto& [id, pkg] : packaged) {
        wanted[id] = true;
        std::string dir = (fs::path(cache_dir) / id.str()).string();
        std::string marker_path = (fs::path(dir) / ".ttmod-cache").string();
        std::string want_marker = marker_for(pkg);
        std::string have_marker = read_file(marker_path);
        if (!want_marker.empty() && have_marker == want_marker && fs::is_directory(dir, ec)) {
            out.effective[id] = dir;
            continue; // fresh
        }

        // Transactional replacement. The live cache is NOT touched until the
        // new one is fully extracted, validated and marked, so a failure or
        // crash leaves the previous version loadable.
        std::string stage = staging_dir(dir);
        std::string retired = retired_dir(dir);
        fs::remove_all(stage, ec);
        fs::remove_all(retired, ec);
        auto fail_stage = [&](const std::string& why) {
            out.log.push_back(id.str() + ": cache refresh failed: " + why);
            fs::remove_all(stage, ec);
        };
        auto ex = extract_package(pkg, stage);
        if (!ex.ok()) {
            fail_stage(ex.error().message);
            continue;
        }
        if (!ttmod::file_io::write_file_atomic((fs::path(stage) / ".ttmod-cache").string(), want_marker)) {
            fail_stage("cache marker unwritable");
            continue;
        }
        // Swap: move the live dir aside, move the staged one in, drop the old.
        // The window between the two renames is what the recovery sweep
        // above closes.
        bool had_live = fs::is_directory(dir, ec);
        if (had_live) {
            fs::rename(dir, retired, ec);
            if (ec) {
                fail_stage("cannot retire old cache: " + ec.message());
                continue;
            }
        }
        fs::rename(stage, dir, ec);
        if (ec) {
            std::string why = "cache replace failed: " + ec.message();
            if (had_live) fs::rename(retired, dir, ec); // put the old one back
            fail_stage(why);
            continue;
        }
        if (had_live) fs::remove_all(retired, ec);
        out.log.push_back(id.str() + ": cached from package");
        out.effective[id] = dir;
    }
    // Stale cleanup: our-marker dirs with no source package go away.
    for (auto& e : fs::directory_iterator(cache_dir, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        std::string marker = (e.path() / ".ttmod-cache").string();
        if (!fs::exists(marker, ec)) continue; // not ours: never touch
        std::string id = e.path().filename().string();
        if (has_retired_suffix(id) && !entry_id(id)) continue;
        auto parsed_id = ModId::parse(id);
        if (!parsed_id.ok() || wanted.find(parsed_id.value()) == wanted.end()) {
            fs::remove_all(e.path(), ec);
            out.log.push_back((parsed_id.ok() ? parsed_id.value().str() : id) + ": stale cache removed");
        }
    }
    return Result<CacheSync>::ok(std::move(out));
}

} // namespace ttmod