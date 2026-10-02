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
    if (code.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kCapacity) return false;
    g_queue.push_back(code);
    g_history.push_back(code);
    if (g_history.size() > kHistory) g_history.erase(g_history.begin());
    return true;
}

std::vector<std::string> uiqueue_take() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out;
    out.swap(g_queue);
    return out;
}

std::vector<std::string> uiqueue_recent() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_history;
}

} // namespace ttmod
