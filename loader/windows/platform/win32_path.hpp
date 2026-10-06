#pragma once
// Win32-side path adapter (Windows-only). The deep portable normalizer
// lives in core/ (ttmod::normalize_win_path) — this header only covers
// Win32 API glue: UTF-8 narrow/widen (dynamic, long-path aware), '/'
// -> '\' conversion, backslash join, dir-of, module path, W file-exists
// and library load. Header-inline, no .cpp.
#ifdef _WIN32
#include <windows.h>

#include <string>

namespace ttmod_win {

// '/' -> '\' (ASCII-only touch; UTF-8 multibyte sequences pass through).
inline std::string to_win(const std::string& p) {
    std::string w = p;
    for (char& c : w)
        if (c == '/') c = '\\';
    return w;
}

// UTF-8 narrow <-> wide. Normalization only touches ASCII bytes.
inline std::string narrow(const wchar_t* w) {
    if (!w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

inline bool widen(const std::string& s, wchar_t* out) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0 || n > MAX_PATH) return false;
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out, n);
    return true;
}

// Backslash concat (identical semantics to the old `a + "\\" + b` lines).
inline std::string join(const std::string& a, const std::string& b) {
    return to_win(a + "\\" + b);
}

inline std::string dir_of(const char* path) {
    std::string s = path ? path : "";
    size_t i = s.find_last_of("\\/");
    return i == std::string::npos ? std::string(".") : s.substr(0, i);
}

// Exists check for a normalized '/'-separated abs path (converts to '\').
// Wide API: correct for non-ASCII paths (the A version mapped through the
// ANSI codepage).
inline bool exists(const std::string& norm_abs_slashes) {
    std::wstring win;
    {
        std::string s = to_win(norm_abs_slashes);
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 0) return false;
        win.assign((size_t)n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, win.data(), n);
    }
    DWORD a = GetFileAttributesW(win.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Module path as UTF-8 (dynamic buffer: no MAX_PATH truncation).
inline std::string module_path(HMODULE mod) {
    std::wstring w(32768, L'\0');
    DWORD n = GetModuleFileNameW(mod, w.data(), (DWORD)w.size());
    if (n == 0 || n >= w.size()) return "";
    w.resize(n);
    int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (m <= 1) return "";
    std::string s((size_t)m - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), m, nullptr, nullptr);
    return s;
}

// LoadLibrary for a UTF-8 path (non-ASCII install dirs).
inline HMODULE load_library(const std::string& utf8path) {
    std::wstring w;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8path.c_str(), -1, nullptr, 0);
        if (n <= 0) return nullptr;
        w.assign((size_t)n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8path.c_str(), -1, w.data(), n);
    }
    return LoadLibraryW(w.c_str());
}

} // namespace ttmod_win
#endif
