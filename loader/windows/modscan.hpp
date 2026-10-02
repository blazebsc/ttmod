// Shared mod entry shape (framework builds it from canonical discovery;
// loaders consume it). Replaces per-loader directory scans.
#pragma once
#ifdef _WIN32
#include <string>
#include "ttmod/manifest.hpp"
namespace ttmod_win {
struct ScannedMod {
    std::string dir; // effective dir (unpacked dir or ttmod/cache/<id>)
    bool has_dll = false; // resolved native plugin exists
    bool packaged = false; // came from a .ttmod (dir = cache)
    std::string plugin_rel; // manifest "plugin" or legacy "plugin.dll"
    ttmod::ModManifest manifest;
};
} // namespace ttmod_win
#endif
