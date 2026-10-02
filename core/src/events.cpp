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
    int t = next_++;
    subs_[event_id].emplace_back(t, std::move(cb));
    return t;
}

void EventBus::unsubscribe(int token) {
    for (auto& [id, v] : subs_)
        for (auto it = v.begin(); it != v.end(); ++it)
            if (it->first == token) {
                v.erase(it);
                return;
            }
}

void EventBus::dispatch(const FileEvent& ev) const {
    auto it = subs_.find(ev.id);
    if (it == subs_.end()) return;
    // Snapshot size only; callbacks must not mutate the bus (documented).
    for (auto& [tok, cb] : it->second) cb(ev);
}

std::vector<EventBus::Cb> EventBus::snapshot(int event_id) const {
    std::vector<Cb> out;
    auto it = subs_.find(event_id);
    if (it == subs_.end()) return out;
    for (auto& [tok, cb] : it->second) out.push_back(cb);
    return out;
}

bool EventBus::has(int event_id) const {
    auto it = subs_.find(event_id);
    return it != subs_.end() && !it->second.empty();
}

} // namespace ttmod
