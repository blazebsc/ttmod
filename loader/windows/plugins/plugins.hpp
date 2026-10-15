// Plugin loader. Takes the discovery-built mod list (shared with the
// resource indexer) so depends/conflicts see every mod.
#pragma once
#ifdef _WIN32
#include <string>
#include <vector>
#include "modscan.hpp"
#include "ttmod/runtime_owner.hpp"
namespace ttmod_win {
void plugins_init(const std::vector<ScannedMod>& all, const char* profile_id, const char* game, int season,
                  const char* log_path, ttmod::RuntimeOwner* owner);
// PR 31 wiring: a second, Luau script VM process-lifetime-owned by the
// framework. Plugin register_api installs on it too, so ttmod.native.*
// bindings work from Luau mods. Null (default) = no Luau VM.
void plugins_set_luau_vm(ttmod::ScriptVm* vm);
} // namespace ttmod_win
#endif