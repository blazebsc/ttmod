#pragma once
// Win32-side path adapter (shallow, Windows-only). The deep portable
// normalizer lives in core/ (ttmod::normalize_win_path) — this header only
// covers Win32 API glue that was copy-pasted across loader/windows files:
// UTF-8 narrow/widen (MAX_PATH semantics), '/' -> '\' conversion,
// backslash join, dir-of, and file-exists. Header-inline, no .cpp.
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
inline bool exists(const std::string& norm_abs_slashes) {
    std::string win = to_win(norm_abs_slashes);
    DWORD a = GetFileAttributesA(win.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace ttmod_win
#endif
