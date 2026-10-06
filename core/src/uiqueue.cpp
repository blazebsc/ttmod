// See uiqueue.hpp. std::mutex keeps this portable (native tests run it
// on Linux; the Win32 layer just calls push/take).
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
    return true;
}

std::vector<std::string> uiqueue_take() {
    std::vector<std::string> out;
    for (auto& c : uiqueue_take_cmd()) out.push_back(c.code);
    return out;
}

std::vector<UiCommand> uiqueue_take_cmd() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<UiCommand> out;
    for (auto& code : g_queue) out.push_back(UiCommand{UiCommandType::ExecuteChunk, code});
    g_queue.clear();
    return out;
}

std::vector<std::string> uiqueue_recent() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_history;
}

} // namespace ttmod
