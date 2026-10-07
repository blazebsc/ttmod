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
} // namespace ttmod_win
#endif