// Mod index. Takes the discovery-built mod list (shared with the plugin
// loader) and builds the ttmod::Resolver used by the CreateFileW hook.
#pragma once
#ifdef _WIN32
#include <string>
#include <vector>
#include "modscan.hpp"
#include "ttmod/discovery.hpp"
#include "ttmod/manifest.hpp"
#include "ttmod/plugin_api.h"
namespace ttmod_win {
// Builds the process-wide resolver. Safe to call once at init.
void mods_init(const std::vector<ScannedMod>& all, const char* game_root, const char* log_path);
// Menu store: enabled entries + disabled ones (from discovery). Powers ABI v4.
void mods_store_menu(const std::vector<ScannedMod>& enabled,
                     const std::vector<ttmod::Discovered>& disabled);
int mods_menu_count();
int mods_menu_info(int index, ttmod_modinfo* out);
// Full manifest for snapshot assembly (internal menu bridge only, never ABI).
bool mods_menu_manifest(int index, ttmod::ModManifest* out);
// Hot-path lookup: requested = original CreateFileW path (wide).
// Returns true + out_path (wide, caller-sized MAX_PATH buffer) on override.
bool mods_resolve(const wchar_t* requested, wchar_t* out_path);
// Narrow core used by the event path: resolve + diagnostics without conversion.
bool mods_try(const wchar_t* requested, std::string& out_requested, std::string& out_replacement,
              std::string& out_winner);
} // namespace ttmod_win
#endif
