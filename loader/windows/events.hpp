// M7 event bridge (Windows-only): owns the process EventBus, implements the
// host subscribe/unsubscribe ABI, and exposes dispatch for the file hooks.
// Dispatch is synchronous on the caller (game) thread; plugin callbacks must
// be non-blocking and reentrant. Event strings live only during the callback.
#pragma once
#ifdef _WIN32
#include <cstddef>
#include <string>
#include "ttmod/plugin_api.h"
namespace ttmod_win {
void events_init(const char* log_path);
// Called by hooks after the real open returns. Cheap when no subscribers.
void dispatch_file_event(const std::string& requested_utf8, const std::string& normalized,
                         const std::string& resolved, const std::string& winner,
                         const std::string& category, bool overridden, bool succeeded);
// ABI thunks (stored in ttmod_host).
int events_subscribe(int event_id, ttmod_event_cb cb, void* ctx);
void events_unsubscribe(int token);
int events_get_state(ttmod_state* out);
// Detach-safe one-line summary into caller buffer (no IO/alloc).
int events_state_summary(char* buf, size_t len);
} // namespace ttmod_win
#endif
