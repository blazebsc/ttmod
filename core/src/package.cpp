// .ttmod package handling (portable, miniz). See package.hpp.
#include "ttmod/package.hpp"
#include "ttmod/file_io.hpp"
#include "ttmod/pathnorm.hpp"
#include "ttmod/validate.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "miniz.h"

namespace ttmod {
namespace {

namespace fs = std::filesystem;

// Unix file-type bits (local copy: MSVC has no sys/stat S_IFLNK).
constexpr unsigned kUnixIFMT = 0170000;
constexpr unsigned kUnixIFREG = 0100000;
constexpr unsigned kUnixIFDIR = 0040000;

// Validate one archive entry name. Returns normalized form or "" if unsafe.
// Single implementation: validate_mod_relative_path (core/validate.*).
// Case is PRESERVED here (Windows targets are case-insensitive, but entries
// may legitimately use mixed case).
std::string safe_entry_nocase(const std::string& raw) {
    auto r = validate_mod_relative_path(raw);
    if (!r.ok()) return "";
    return r.value();
}

// Dedupe key: safe_entry_nocase + ASCII lowercase.
std::string safe_entry(const std::string& raw) {
    std::string o = safe_entry_nocase(raw);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return o;
}

struct OpenZip {
    mz_zip_archive zip{};
    bool ok = false;
    explicit OpenZip(const std::string& path) {
        memset(&zip, 0, sizeof zip);
        ok = mz_zip_reader_init_file(&zip, path.c_str(), 0) != 0;
    }
    ~OpenZip() {
        if (ok) mz_zip_reader_end(&zip);
    }
};

// Components in a raw archive name ('/' separated; '\' too, since a
// Windows-authored zip may carry either).
size_t count_components(const std::string& raw) {
    size_t n = 0;
    bool in = false;
    for (char c : raw) {
        if (c == '/' || c == '\\') {
            in = false;
            continue;
        }
        if (!in) {
            ++n;
            in = true;
        }
    }
    return n;
}

Result<std::string> read_entry(mz_zip_archive& zip, mz_uint idx) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, idx, &st))
        return Result<std::string>::fail(
            Error{"read-entry", std::to_string(idx), errcat::kIO, "cannot read zip entry"});
    size_t n = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, idx, &n, 0);
    if (!p)
        return Result<std::string>::fail(
            Error{"read-entry", std::to_string(idx), errcat::kIO, "cannot read zip entry"});
    std::string out((const char*)p, n);
    mz_free(p);
    return Result<std::string>::ok(std::move(out));
}

} // namespace

Result<PackView> inspect_package(const std::string& path) {
    PackView v;
    auto fail = [&](const std::string& msg, const char* cat) {
        return Result<PackView>::fail(Error{"inspect-package", path, cat, msg});
    };
    // All resource limits live in package_policy.hpp. Nothing in this file
    // may invent its own bound: a second number here would drift.
    std::error_code pec;
    uint64_t file_sz = fs::file_size(path, pec);
    if (pec || file_sz > packlimits::kMaxPackageBytes) {
        return fail("package too large", errcat::kLimit);
    }
    OpenZip z(path);
    if (!z.ok) {
        return fail("cannot open archive", errcat::kIO);
    }
    mz_uint n = mz_zip_reader_get_num_files(&z.zip);
    if (n == 0 || n > packlimits::kMaxEntries) {
        return fail("bad entry count", errcat::kLimit);
    }
    uint64_t total_uncomp = 0;
    std::vector<std::string> names; // normalized, for dedupe
    std::string manifest;
    int manifests = 0;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z.zip, i, &st)) {
            return fail("unreadable central directory", errcat::kIO);
        }
        std::string raw = st.m_filename;
        bool is_dir = st.m_is_directory != 0 || (!raw.empty() && raw.back() == '/');
        // Symlink/special rejection via unix mode bits when present.
        unsigned mode = (st.m_external_attr >> 16) & 0xFFFF;
        if (mode != 0) {
            unsigned ft = mode & kUnixIFMT;
            if (ft != 0 && ft != kUnixIFREG && ft != kUnixIFDIR) {
                return fail(std::string("unsafe special entry: ") + raw, errcat::kTraversal);
            }
        }
        if (is_dir) continue; // dirs implicit; validated via file paths
        // Raw name bounds BEFORE normalization: a pathological name can
        // normalize to something short.
        if (raw.size() > packlimits::kMaxPathChars) {
            return fail(std::string("entry path too long: ") + raw.substr(0, 64), errcat::kLimit);
        }
        if (count_components(raw) > packlimits::kMaxPathDepth) {
            return fail(std::string("entry path too deep: ") + raw.substr(0, 64), errcat::kLimit);
        }
        if (st.m_uncomp_size > packlimits::kMaxEntryBytes) {
            return fail(std::string("entry too large: ") + raw, errcat::kLimit);
        }
        // Saturating add: a lying m_uncomp_size must not wrap into "small".
        total_uncomp += st.m_uncomp_size;
        if (total_uncomp > packlimits::kMaxTotalUncompressedBytes) {
            return fail("package total too large", errcat::kLimit);
        }
        std::string norm = safe_entry(raw);
        if (norm.empty()) {
            return fail(std::string("unsafe path: ") + raw, errcat::kTraversal);
        }
        for (auto& e : names) {
            if (e == norm) {
                return fail(std::string("duplicate entry: ") + raw, errcat::kDuplicate);
            }
        }
        names.push_back(norm);
        if (norm == "manifest.json") {
            manifests++;
            if (st.m_uncomp_size > packlimits::kMaxManifestBytes) {
                return fail("manifest too large", errcat::kLimit);
            }
            auto entry = read_entry(z.zip, i);
            if (!entry.ok()) {
                return fail("cannot read manifest.json", errcat::kIO);
            }
            manifest = std::move(entry).value();
            continue;
        }
        v.files.push_back({norm, st.m_uncomp_size});
    }
    if (manifests == 0) {
        return fail("manifest.json missing", errcat::kMissing);
    }
    if (manifests > 1) {
        return fail("multiple manifest.json entries", errcat::kDuplicate);
    }
    v.manifest_text = manifest;
    // Manifest must parse; every declared file + plugin path must be present.
    Result<ModManifest> pm = parse_manifest(manifest);
    if (!pm.ok()) return Result<PackView>::fail(pm.error());
    ModManifest m = pm.value();
    auto has = [&](const std::string& rel) {
        std::string k = safe_entry(rel);
        if (k.empty()) return false;
        for (auto& e : names)
            if (e == k) return true;
        return false;
    };
    for (auto& [from, to] : m.overrides.files) {
        if (!has(to)) {
            return fail(std::string("declared file missing: ") + to, errcat::kMissing);
        }
    }
    if (!m.plugin.path.empty() && !has(m.plugin.path)) {
        return fail(std::string("declared plugin missing: ") + m.plugin.path, errcat::kMissing);
    }
    return Result<PackView>::ok(std::move(v));
}

Result<void> extract_package(const std::string& path, const std::string& dest_dir) {
    auto insp = inspect_package(path);
    if (!insp.ok()) return Result<void>::fail(insp.error());
    auto fail = [&](const std::string& msg, const char* cat) {
        return Result<void>::fail(Error{"extract-package", path, cat, msg});
    };
    std::error_code ec;
    if (fs::exists(dest_dir, ec)) {
        if (!fs::is_empty(dest_dir, ec)) {
            return fail("destination not empty", errcat::kIO);
        }
    } else if (!fs::create_directories(dest_dir, ec)) {
        return fail("cannot create destination", errcat::kIO);
    }
    auto fail_cleanup = [&](const std::string& msg, const char* cat) {
        fs::remove_all(dest_dir, ec);
        return Result<void>::fail(Error{"extract-package", path, cat, msg});
    };
    OpenZip z(path);
    if (!z.ok) return fail_cleanup("cannot reopen archive", errcat::kIO);
    mz_uint n = mz_zip_reader_get_num_files(&z.zip);
    if (n == 0 || n > packlimits::kMaxEntries) {
        return fail_cleanup("bad entry count", errcat::kLimit);
    }
    // The archive was inspected moments ago but is read again here, so the
    // file on disk is not provably the same one that was validated. Limits
    // are re-enforced here rather than trusting the earlier pass.
    uint64_t total_uncomp = 0;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z.zip, i, &st)) return fail_cleanup("central directory changed", errcat::kIO);
        std::string raw = st.m_filename;
        bool is_dir = st.m_is_directory != 0 || (!raw.empty() && raw.back() == '/');
        if (is_dir) continue;
        if (raw.size() > packlimits::kMaxPathChars || count_components(raw) > packlimits::kMaxPathDepth) {
            return fail_cleanup("entry path out of bounds: " + raw.substr(0, 64), errcat::kLimit);
        }
        if (st.m_uncomp_size > packlimits::kMaxEntryBytes) {
            return fail_cleanup(std::string("entry too large: ") + raw, errcat::kLimit);
        }
        total_uncomp += st.m_uncomp_size;
        if (total_uncomp > packlimits::kMaxTotalUncompressedBytes) {
            return fail_cleanup("package total too large", errcat::kLimit);
        }
        std::string norm = safe_entry_nocase(raw);
        if (norm.empty()) return fail_cleanup(std::string("unsafe path: ") + raw, errcat::kTraversal);
        fs::path out = fs::path(dest_dir) / norm;
        fs::create_directories(out.parent_path(), ec);
        if (ec) return fail_cleanup("cannot create directory", errcat::kIO);
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&z.zip, i, &sz, 0);
        if (!p) return fail_cleanup(std::string("extract failed: ") + raw, errcat::kIO);
        // Trust the bytes we got, not the declared size.
        if ((uint64_t)sz > packlimits::kMaxEntryBytes || total_uncomp > packlimits::kMaxTotalUncompressedBytes) {
            mz_free(p);
            return fail_cleanup(std::string("entry exceeds limit: ") + raw, errcat::kLimit);
        }
        FILE* f = ttmod::file_io::open_write(out.string());
        if (!f) {
            mz_free(p);
            return fail_cleanup(std::string("cannot write: ") + raw, errcat::kIO);
        }
        size_t w = fwrite(p, 1, sz, f);
        fclose(f);
        mz_free(p);
        if (w != sz) return fail_cleanup(std::string("short write: ") + raw, errcat::kIO);
    }
    // Re-validate the result: manifest present + parseable.
    std::string mt;
    {
        FILE* f = ttmod::file_io::open_read((fs::path(dest_dir) / "manifest.json").string());
        if (!f) return fail_cleanup("extracted manifest missing", errcat::kMissing);
        char buf[4096];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) mt.append(buf, r);
        fclose(f);
    }
    if (!parse_manifest(mt).ok()) return fail_cleanup("extracted manifest invalid", errcat::kSyntax);
    return Result<void>::success();
}

// Content policy: package these top-level names; skip the rest silently
// (.git/build/logs/cache/dotfiles/machine junk are never mod content;
// documented in docs/runtime/mod-packages.md). Root plugin.dll is the
// legacy native layout (manifest "plugin" field preferred for new mods).
static bool wanted_top(const std::string& top) {
    for (auto* w : {"manifest.json", "plugin.dll", "files", "plugins", "scripts", "assets", "docs", "README.md",
                    "README.txt", "LICENSE", "LICENSE.txt"})
        if (top == w) return true;
    return false;
}

Result<void> create_package(const std::string& src_dir, const std::string& out_path) {
    auto fail = [&](const std::string& msg, const char* cat) {
        return Result<void>::fail(Error{"create-package", src_dir, cat, msg});
    };
    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) {
        return fail("source not a directory", errcat::kIO);
    }
    if (!fs::exists(fs::path(src_dir) / "manifest.json", ec)) {
        return fail("manifest.json missing in source", errcat::kMissing);
    }
    // Collect regular files, sorted, skipping policy-excluded + junk.
    std::vector<std::string> rels;
    for (auto it = fs::recursive_directory_iterator(src_dir, ec); it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            return fail("directory walk failed", errcat::kIO);
        }
        if (!it->is_regular_file()) continue;
        std::string rel = fs::relative(it->path(), src_dir, ec).generic_string();
        if (ec || rel.empty()) continue;
        std::string top = rel.substr(0, rel.find('/'));
        if (!wanted_top(top)) continue; // .git/build/logs/cache/etc. excluded
        std::string leaf = rel.substr(rel.find_last_of('/') + 1);
        if (!leaf.empty() && leaf[0] == '.') continue; // dotfiles excluded
        std::string key = safe_entry(rel);             // lowercase key: case-collisions rejected
        if (key.empty()) {
            return fail(std::string("unsafe name in source: ") + rel, errcat::kTraversal);
        }
        for (auto& done : rels)
            if (safe_entry(done) == key) {
                return fail(std::string("ambiguous names in source: ") + rel, errcat::kDuplicate);
            }
        rels.push_back(rel);
    }
    std::sort(rels.begin(), rels.end());
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, out_path.c_str(), 0)) {
        return fail("cannot open output", errcat::kIO);
    }
    auto fail_cleanup = [&](const std::string& msg, const char* cat) {
        mz_zip_writer_end(&zip);
        std::error_code ec2;
        fs::remove(out_path, ec2);
        return Result<void>::fail(Error{"create-package", src_dir, cat, msg});
    };
    // Fixed timestamp (1980-01-01) + sorted entries + fixed level =>
    // deterministic bytes (modulo local TZ in miniz's time conversion;
    // tests pin TZ=UTC).
    MZ_TIME_T fixed_t = (MZ_TIME_T)315532800;
    for (auto& rel : rels) {
        std::string full = (fs::path(src_dir) / rel).string();
        FILE* f = ttmod::file_io::open_read(full);
        if (!f) return fail_cleanup(std::string("cannot read: ") + rel, errcat::kIO);
        std::string data;
        char buf[65536];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, r);
        fclose(f);
        if (!mz_zip_writer_add_mem_ex_v2(&zip, rel.c_str(), data.data(), data.size(), nullptr, 0,
                                         MZ_DEFAULT_COMPRESSION, 0, 0, &fixed_t, nullptr, 0, nullptr, 0))
            return fail_cleanup(std::string("cannot add: ") + rel, errcat::kIO);
    }
    if (!mz_zip_writer_finalize_archive(&zip)) return fail_cleanup("finalize failed", errcat::kIO);
    mz_zip_writer_end(&zip);
    return Result<void>::success();
}

} // namespace ttmod
