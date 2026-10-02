// M2 hook stage (Windows-only). Resolves ScriptManager::LoadResource for the
// mcsm1_pc_x86 profile, validates it IN MEMORY (disk .text is packed), then
// installs a log-and-passthrough MinHook detour. Any validation failure:
// log the reason, install nothing, game continues.
#pragma once
#ifdef _WIN32
namespace ttmod_win {
void hooks_init(const char* profile_id, const char* log_path);
} // namespace ttmod_win
#endif
