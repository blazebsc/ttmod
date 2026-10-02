// See uiqueue.hpp. std::mutex keeps this portable (native tests run it
// on Linux; the Win32 layer just calls push/take).
#include <mutex>

#include "ttmod/uiqueue.hpp"

namespace ttmod {
namespace {
constexpr size_t kCapacity = 16;
std::mutex g_mutex;
std::vector<std::string> g_queue;
} // namespace

bool uiqueue_push(const std::string& code) {
    if (code.empty()) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() >= kCapacity) return false;
    g_queue.push_back(code);
    return true;
}

std::vector<std::string> uiqueue_take() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string> out;
    out.swap(g_queue);
    return out;
}

} // namespace ttmod
