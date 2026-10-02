#include "ttmod/events.hpp"

namespace ttmod {

std::string classify_path(const std::string& normalized) {
    auto slash = normalized.find_last_of('/');
    std::string base = slash == std::string::npos ? normalized : normalized.substr(slash + 1);
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".lua") == 0 &&
        (base.compare(0, 9, "_resdesc_") == 0 || base.compare(0, 10, "_rescdesc_") == 0))
        return "resdesc";
    if (base.size() > 8 && base.compare(base.size() - 8, 8, ".ttarch2") == 0) return "archive";
    return "other";
}

int EventBus::subscribe(int event_id, Cb cb) {
    std::lock_guard<std::mutex> l(mtx_);
    int t = next_++;
    subs_[event_id].emplace_back(t, std::move(cb));
    return t;
}

void EventBus::unsubscribe(int token) {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto& [id, v] : subs_)
        for (auto it = v.begin(); it != v.end(); ++it)
            if (it->first == token) {
                v.erase(it);
                return;
            }
}

void EventBus::dispatch(const FileEvent& ev) const {
    std::vector<Cb> cbs = snapshot(ev.id); // locks internally, copy only
    for (auto& cb : cbs) cb(ev); // WITHOUT the lock: re-entry safe
}

std::vector<EventBus::Cb> EventBus::snapshot(int event_id) const {
    std::lock_guard<std::mutex> l(mtx_);
    std::vector<Cb> out;
    auto it = subs_.find(event_id);
    if (it == subs_.end()) return out;
    for (auto& [tok, cb] : it->second) out.push_back(cb);
    return out;
}

bool EventBus::has(int event_id) const {
    std::lock_guard<std::mutex> l(mtx_);
    auto it = subs_.find(event_id);
    return it != subs_.end() && !it->second.empty();
}

} // namespace ttmod
