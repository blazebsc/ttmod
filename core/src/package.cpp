// .ttmod package handling (portable, miniz). See package.hpp.
#include "ttmod/package.hpp"
#include "ttmod/pathnorm.hpp"

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
// Rules: no drive/colon, no leading separator, no UNC/extended prefix,
// no ".." that escapes. Case is PRESERVED here (Windows targets are
// case-insensitive, but entries may legitimately use mixed case).
std::string safe_entry_nocase(const std::string& raw) {
    if (raw.empty() || raw.size() > 512) return "";
    std::string s = raw;
    for (char& c : s)
        if (c == '\\') c = '/';
    if (s.find(':') != std::string::npos) return "";
    if (s[0] == '/' || s[0] == '~') return "";
    if (s.compare(0, 2, "//") == 0) return "";
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = i < s.size() ? s[i] : '/';
        if (c == '/') {
            if (!cur.empty() && cur != ".") {
                if (cur == "..") {
                    if (parts.empty()) return ""; // escape
                    parts.pop_back();
                } else {
                    parts.push_back(cur);
                }
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    std::string o;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) o += '/';
        o += parts[i];
    }
    if (parts.size() > 32) return ""; // absurd depth: hostile or broken
    return o;
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

bool read_entry(mz_zip_archive& zip, mz_uint idx, std::string& out) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, idx, &st)) return false;
    size_t n = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip, idx, &n, 0);
    if (!p) return false;
    out.assign((const char*)p, n);
    mz_free(p);
    return true;
}

} // namespace

PackView inspect_package(const std::string& path) {
    PackView v;
    OpenZip z(path);
    if (!z.ok) {
        v.error = "cannot open archive";
        return v;
    }
    mz_uint n = mz_zip_reader_get_num_files(&z.zip);
    if (n == 0 || n > 4096) {
        v.error = "bad entry count";
        return v;
    }
    std::vector<std::string> names; // normalized, for dedupe
    std::string manifest;
    int manifests = 0;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z.zip, i, &st)) {
            v.error = "unreadable central directory";
            return v;
        }
        std::string raw = st.m_filename;
        bool is_dir = st.m_is_directory != 0 || (!raw.empty() && raw.back() == '/');
        // Symlink/special rejection via unix mode bits when present.
        unsigned mode = (st.m_external_attr >> 16) & 0xFFFF;
        if (mode != 0) {
            unsigned ft = mode & kUnixIFMT;
            if (ft != 0 && ft != kUnixIFREG && ft != kUnixIFDIR) {
                v.error = std::string("unsafe special entry: ") + raw;
                return v;
            }
        }
        if (is_dir) continue; // dirs implicit; validated via file paths
        if (st.m_uncomp_size > (64ull << 20)) {
            v.error = std::string("entry too large: ") + raw;
            return v;
        }
        std::string norm = safe_entry(raw);
        if (norm.empty()) {
            v.error = std::string("unsafe path: ") + raw;
            return v;
        }
        for (auto& e : names) {
            if (e == norm) {
                v.error = std::string("duplicate entry: ") + raw;
                return v;
            }
        }
        names.push_back(norm);
        if (norm == "manifest.json") {
            manifests++;
            if (!read_entry(z.zip, i, manifest)) {
                v.error = "cannot read manifest.json";
                return v;
            }
            continue;
        }
        v.files.push_back({norm, st.m_uncomp_size});
    }
    if (manifests == 0) {
        v.error = "manifest.json missing";
        return v;
    }
    if (manifests > 1) {
        v.error = "multiple manifest.json entries";
        return v;
    }
    v.manifest_text = manifest;
    // Manifest must parse; every declared file + plugin path must be present.
    ModManifest m = parse_manifest(manifest);
    if (!m.ok) {
        v.error = std::string("manifest invalid: ") + m.error;
        return v;
    }
    auto has = [&](const std::string& rel) {
        std::string k = safe_entry(rel);
        if (k.empty()) return false;
        for (auto& e : names)
            if (e == k) return true;
        return false;
    };
    for (auto& [from, to] : m.files) {
        if (!has(to)) {
            v.error = std::string("declared file missing: ") + to;
            return v;
        }
    }
    if (!m.plugin.empty() && !has(m.plugin)) {
        v.error = std::string("declared plugin missing: ") + m.plugin;
        return v;
    }
    v.ok = true;
    return v;
}

bool extract_package(const std::string& path, const std::string& dest_dir, std::string& error) {
    PackView v = inspect_package(path);
    if (!v.ok) {
        error = v.error;
        return false;
    }
    std::error_code ec;
    if (fs::exists(dest_dir, ec)) {
        if (!fs::is_empty(dest_dir, ec)) {
            error = "destination not empty";
            return false;
        }
    } else if (!fs::create_directories(dest_dir, ec)) {
        error = "cannot create destination";
        return false;
    }
    auto fail = [&](const std::string& e) {
        error = e;
        fs::remove_all(dest_dir, ec);
        return false;
    };
    OpenZip z(path);
    if (!z.ok) return fail("cannot reopen archive");
    mz_uint n = mz_zip_reader_get_num_files(&z.zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z.zip, i, &st)) return fail("central directory changed");
        std::string raw = st.m_filename;
        bool is_dir = st.m_is_directory != 0 || (!raw.empty() && raw.back() == '/');
        if (is_dir) continue;
        std::string norm = safe_entry_nocase(raw);
        if (norm.empty()) return fail(std::string("unsafe path: ") + raw);
        fs::path out = fs::path(dest_dir) / norm;
        fs::create_directories(out.parent_path(), ec);
        if (ec) return fail("cannot create directory");
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&z.zip, i, &sz, 0);
        if (!p) return fail(std::string("extract failed: ") + raw);
        FILE* f = fopen(out.string().c_str(), "wb");
        if (!f) {
            mz_free(p);
            return fail(std::string("cannot write: ") + raw);
        }
        size_t w = fwrite(p, 1, sz, f);
        fclose(f);
        mz_free(p);
        if (w != sz) return fail(std::string("short write: ") + raw);
    }
    // Re-validate the result: manifest present + parseable.
    std::string mt;
    {
        FILE* f = fopen((fs::path(dest_dir) / "manifest.json").string().c_str(), "rb");
        if (!f) return fail("extracted manifest missing");
        char buf[4096];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) mt.append(buf, r);
        fclose(f);
    }
    if (!parse_manifest(mt).ok) return fail("extracted manifest invalid");
    return true;
}

// Content policy: package these top-level names; skip the rest silently
// (.git/build/logs/cache/dotfiles/machine junk are never mod content;
// documented in docs/runtime/mod-packages.md). Root plugin.dll is the
// legacy native layout (manifest "plugin" field preferred for new mods).
static bool wanted_top(const std::string& top) {
    for (auto* w : {"manifest.json", "plugin.dll", "files", "plugins", "scripts", "assets", "docs",
                    "README.md", "README.txt", "LICENSE", "LICENSE.txt"})
        if (top == w) return true;
    return false;
}

bool create_package(const std::string& src_dir, const std::string& out_path, std::string& error) {
    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) {
        error = "source not a directory";
        return false;
    }
    if (!fs::exists(fs::path(src_dir) / "manifest.json", ec)) {
        error = "manifest.json missing in source";
        return false;
    }
    // Collect regular files, sorted, skipping policy-excluded + junk.
    std::vector<std::string> rels;
    for (auto it = fs::recursive_directory_iterator(src_dir, ec); it != fs::recursive_directory_iterator();
         ++it) {
        if (ec) {
            error = "directory walk failed";
            return false;
        }
        if (!it->is_regular_file()) continue;
        std::string rel = fs::relative(it->path(), src_dir, ec).generic_string();
        if (ec || rel.empty()) continue;
        std::string top = rel.substr(0, rel.find('/'));
        if (!wanted_top(top)) continue; // .git/build/logs/cache/etc. excluded
        std::string leaf = rel.substr(rel.find_last_of('/') + 1);
        if (!leaf.empty() && leaf[0] == '.') continue; // dotfiles excluded
        std::string key = safe_entry(rel); // lowercase key: case-collisions rejected
        if (key.empty()) {
            error = std::string("unsafe name in source: ") + rel;
            return false;
        }
        for (auto& done : rels)
            if (safe_entry(done) == key) {
                error = std::string("ambiguous names in source: ") + rel;
                return false;
            }
        rels.push_back(rel);
    }
    std::sort(rels.begin(), rels.end());
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, out_path.c_str(), 0)) {
        error = "cannot open output";
        return false;
    }
    auto fail = [&](const std::string& e) {
        error = e;
        mz_zip_writer_end(&zip);
        std::error_code ec2;
        fs::remove(out_path, ec2);
        return false;
    };
    // Fixed timestamp (1980-01-01) + sorted entries + fixed level =>
    // deterministic bytes (modulo local TZ in miniz's time conversion;
    // tests pin TZ=UTC).
    MZ_TIME_T fixed_t = (MZ_TIME_T)315532800;
    for (auto& rel : rels) {
        std::string full = (fs::path(src_dir) / rel).string();
        FILE* f = fopen(full.c_str(), "rb");
        if (!f) return fail(std::string("cannot read: ") + rel);
        std::string data;
        char buf[65536];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, r);
        fclose(f);
        if (!mz_zip_writer_add_mem_ex_v2(&zip, rel.c_str(), data.data(), data.size(), nullptr, 0,
                                         MZ_DEFAULT_COMPRESSION, 0, 0, &fixed_t, nullptr, 0,
                                         nullptr, 0))
            return fail(std::string("cannot add: ") + rel);
    }
    if (!mz_zip_writer_finalize_archive(&zip)) return fail("finalize failed");
    mz_zip_writer_end(&zip);
    return true;
}

} // namespace ttmod
