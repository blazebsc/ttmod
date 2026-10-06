#pragma once
#include <string>

namespace ttmod {

struct ExeInfo; // detect.hpp

// Portable profile selection. No JSON dep; mirrors profiles/*/profile.json.
enum class Architecture { Unknown, X86, X64, Any };

inline Architecture parse_architecture(std::string_view s) {
    if (s == "x86") return Architecture::X86;
    if (s == "x64") return Architecture::X64;
    if (s == "any") return Architecture::Any;
    return Architecture::Unknown;
}

enum class ProfileStatus { Unknown, Supported, DefinedNotImplemented, UnrecognizedBuild };

inline const char* to_string(Architecture a) {
    switch (a) {
        case Architecture::X86: return "x86";
        case Architecture::X64: return "x64";
        case Architecture::Any: return "any";
        default: return "unknown";
    }
}

inline const char* to_string(ProfileStatus s) {
    switch (s) {
        case ProfileStatus::Supported: return "supported";
        case ProfileStatus::DefinedNotImplemented: return "defined-not-implemented";
        case ProfileStatus::UnrecognizedBuild: return "unrecognized-build";
        default: return "unknown";
    }
}

struct GameProfile {
    const char* id = "unknown";      // e.g. "mcsm1_pc_x86"
    const char* game = "unknown";   // e.g. "minecraft-story-mode"
    int season = 0;                  // 1, 2, 0=unknown
    Architecture arch = Architecture::Unknown;
    ProfileStatus status = ProfileStatus::Unknown;
};

GameProfile select_profile(const ExeInfo& e);

// Shared init path used by both the offline tool and the Windows framework DLL.
// Returns the selected profile (id + status); never throws.
// (runtime.hpp, not included here to avoid a cycle, maps status -> mode.)
GameProfile init_from_exe(const std::string& exe_path, const std::string& log_path);

} // namespace ttmod
