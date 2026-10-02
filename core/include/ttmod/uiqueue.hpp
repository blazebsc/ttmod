#pragma once
// Plugin-authored Lua chunks waiting to run on the game's script thread.
// The loader NEVER calls Lua from its own threads (interpreter races); a
// plugin submits source text here and the Lua bridge drains the queue
// inside the ScriptManager::LoadResource hook — same thread, balanced
// stack. Fixed capacity: a flooding plugin drops chunks instead of
// growing memory; the drop is logged by the caller.
#include <string>
#include <vector>

namespace ttmod {

// Queue one chunk (source text). False if the queue is full (dropped) or
// empty input. Never blocks longer than the lock.
bool uiqueue_push(const std::string& code);

// Atomically move out all queued chunks (drain). Empty vector if none.
std::vector<std::string> uiqueue_take();

} // namespace ttmod
