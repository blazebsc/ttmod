#pragma once
// Portable file I/O boundary (Stage G2): project text is UTF-8 everywhere.
// POSIX opens bytes directly (correct); Windows CRT fopen() maps through
// the ANSI codepage (wrong for non-ASCII game paths), so Windows converts
// to UTF-16 and uses the W APIs, with extended-length prefixing past
// legacy MAX_PATH. All core + loader file I/O goes through here - raw
// fopen/_wfopen at call sites is a bug. Header-inline, no .cpp.
#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ttmod {
namespace file_io {

#ifdef _WIN32
// UTF-8 -> wide, dynamic (no MAX_PATH truncation) + extended-length prefix
// for long absolute paths so >260-char game paths keep working. Public so
// loader code can call W APIs directly with the same conversion.
inline std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    for (auto& c : w)
        if (c == L'/') c = L'\\';
    if (w.size() >= 240 && w[1] == L':' && w.compare(0, 4, L"\\\\?\\") != 0)
        w = L"\\\\?\\" + w;
    return w;
}

namespace detail {

inline FILE* open_wide(const std::string& path, const wchar_t* mode) {
    std::wstring w = to_wide(path);
    if (w.empty()) return nullptr;
    FILE* f = nullptr;
    _wfopen_s(&f, w.c_str(), mode);
    return f;
}

} // namespace detail
#endif

inline FILE* open_read(const std::string& path) {
#ifdef _WIN32
    return detail::open_wide(path, L"rb");
#else
    return fopen(path.c_str(), "rb");
#endif
}

inline FILE* open_write(const std::string& path) {
#ifdef _WIN32
    return detail::open_wide(path, L"wb");
#else
    return fopen(path.c_str(), "wb");
#endif
}

inline FILE* open_append(const std::string& path) {
#ifdef _WIN32
    return detail::open_wide(path, L"ab");
#else
    return fopen(path.c_str(), "ab");
#endif
}

inline int rename_file(const std::string& from, const std::string& to) {
#ifdef _WIN32
    std::wstring wfrom = to_wide(from);
    std::wstring wto = to_wide(to);
    if (wfrom.empty() || wto.empty()) return -1;
    if (!MoveFileExW(wfrom.c_str(), wto.c_str(), MOVEFILE_REPLACE_EXISTING)) return -1;
    return 0;
#else
    return std::rename(from.c_str(), to.c_str());
#endif
}

inline int remove_file(const std::string& path) {
#ifdef _WIN32
    std::wstring w = to_wide(path);
    if (w.empty()) return -1;
    return _wremove(w.c_str());
#else
    return std::remove(path.c_str());
#endif
}

} // namespace file_io
} // namespace ttmod
