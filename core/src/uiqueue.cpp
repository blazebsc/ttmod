// See uiqueue.hpp. std::mutex keeps this portable (native tests run it
// on Linux; the Win32 layer just calls push/take).
#include <atomic>
#include <mutex>

#include "ttmod/uiqueue.hpp"

namespace ttmod {
namespace {
constexpr size_t kCapacity = 16;
// Replay history: enough to cover the distinct lua_States the game uses
// (engine + menu; a couple of extra covers a reload).
constexpr size_t kHistory = 64;
std::mutex g_mutex;
std::vector<std::string> g_queue;
std::vector<std::string> g_history;
// PR 23 hot-path: the LoadResource hook drains this on EVERY script load
// (hundreds per session) while plugins submit maybe a dozen chunks total.
// The flag lets take() skip the lock entirely when empty. Ordering: push
// sets it after appending (under lock); take clears it after draining
// (under lock) - both mutations are inside the critical section, so no
// wakeup is ever lost; the flag is only a hint read outside it.
std::atomic<bool> g_nonempty{false};
} // namespace

bool uiqueue_push(const std::string& code) {
    return uiqueue_push_cmd(UiCommand{UiCommandType::ExecuteChunk, code});
}

bool uiqueue_push_cmd(const UiCommand& cmd) {
    if (cmd.type != UiCommandType::ExecuteChunk) return false; // reserved
    if (cmd.code.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kCapacity) return false;
    g_queue.push_back(cmd.code);
    g_history.push_back(cmd.code);
    if (g_history.size() > kHistory) g_history.erase(g_history.begin());
    g_nonempty.store(true, std::memory_order_release);
    return true;
}

std::vector<std::string> uiqueue_take() {
    std::vector<std::string> out;
    for (auto& c : uiqueue_take_cmd()) out.push_back(c.code);
    return out;
}

std::vector<UiCommand> uiqueue_take_cmd() {
    if (!g_nonempty.load(std::memory_order_acquire)) return {}; // fast path: no lock
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<UiCommand> out;
    for (auto& code : g_queue) out.push_back(UiCommand{UiCommandType::ExecuteChunk, code});
    g_queue.clear();
    g_nonempty.store(false, std::memory_order_release);
    return out;
}

std::vector<std::string> uiqueue_recent() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_history;
}

} // namespace ttmod
