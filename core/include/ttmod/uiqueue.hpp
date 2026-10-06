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

// Lua worlds (Stage H): engine scripts and menu scripts run on DIFFERENT
// lua_States. Every bridge operation must be explicit about which domain
// its chunk runs on - globals set on one do not exist on the other.
enum class LuaDomain { Engine, Menu };

// Typed UI commands (Stage H). ExecuteChunk is the wire format today (raw
// Lua source, the v5 plugin ABI); RefreshMenu/SetValue are reserved for a
// future typed protocol. The string push/take/recent API below stays as the
// compatibility surface and carries ExecuteChunk payloads only.
enum class UiCommandType { ExecuteChunk, RefreshMenu, SetValue };

struct UiCommand {
    UiCommandType type = UiCommandType::ExecuteChunk;
    std::string code;
};

// Queue one chunk (source text). False if the queue is full (dropped) or
// empty input. Never blocks longer than the lock.
bool uiqueue_push(const std::string& code);

// Typed submit (future commands must use this, not raw strings).
bool uiqueue_push_cmd(const UiCommand& cmd);

// Atomically move out all queued chunks (drain). Empty vector if none.
std::vector<std::string> uiqueue_take();

// Typed drain.
std::vector<UiCommand> uiqueue_take_cmd();

// Every chunk ever submitted, oldest first. For REPLAY on a second lua_State:
// the game runs menu Lua in a different state than engine Lua, so globals a
// plugin sets during the script-thread drain do not exist where the menu is
// built. Bounded by the same capacity*states worth of history in practice;
// kept separate from g_queue so take() still drains.
std::vector<std::string> uiqueue_recent();

} // namespace ttmod
