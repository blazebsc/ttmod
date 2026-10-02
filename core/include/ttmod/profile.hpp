#pragma once
#include <string>

namespace ttmod {

struct ExeInfo; // detect.hpp

// Portable profile selection. No JSON dep; mirrors profiles/*/profile.json.
struct GameProfile {
    const char* id = "unknown";      // e.g. "mcsm1_pc_x86"
    const char* game = "unknown";    // e.g. "minecraft-story-mode"
    int season = 0;                  // 1, 2, 0=unknown
    const char* arch = "unknown";    // "x86", "x64"
    const char* status = "unknown";  // "supported", "defined-not-implemented", "unknown"
};

GameProfile select_profile(const ExeInfo& e);

// Shared init path used by both the offline tool and the Windows framework DLL.
// Returns selected profile id; never throws.
std::string init_from_exe(const std::string& exe_path, const std::string& log_path);

} // namespace ttmod
