#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ttmod {

// Event ids (stable; mirrored in plugin_api.h).
enum EventId { EV_FILE_OPEN = 1, EV_RESDESC_OPEN = 2, EV_ARCHIVE_OPEN = 3 };

// Precise semantics: a file was REQUESTED and the real open was ATTEMPTED.
// succeeded = real CreateFileW did not return INVALID_HANDLE_VALUE.
// This proves request+attempt, NOT script execution or content consumption.
struct FileEvent {
    int id = EV_FILE_OPEN; // specific id (resdesc/archive specializations)
    std::string requested; // original path as passed by the game (UTF-8)
    std::string normalized; // canonical internal form
    std::string resolved; // replacement path when overridden, else ""
    std::string winner; // winning mod id when overridden, else ""
    std::string category; // "resdesc" | "archive" | "other"
    bool overridden = false;
    bool succeeded = false;
    unsigned long thread = 0; // OS thread id of the caller
};

// Pure classifier on a normalized path's basename. Unit-tested.
std::string classify_path(const std::string& normalized);

// Tiny synchronous bus. Callers that invoke callbacks while holding their own
// locks must use snapshot() and invoke outside the lock: callbacks may
// re-enter subscribe/get_state. Callbacks must be non-blocking and
// reentrancy-safe (file hooks dispatch!).
class EventBus {
public:
    using Cb = std::function<void(const FileEvent&)>;
    // Returns subscription token.
    int subscribe(int event_id, Cb cb);
    void unsubscribe(int token);
    void dispatch(const FileEvent& ev) const;
    bool has(int event_id) const;
    // Copy of subscriber callbacks (for invoking WITHOUT holding any
    // external lock — callbacks may re-enter subscribe/get_state).
    std::vector<Cb> snapshot(int event_id) const;

private:
    int next_ = 1;
    std::map<int, std::vector<std::pair<int, Cb>>> subs_; // id -> (token, cb)
};

} // namespace ttmod
