// M7 event bridge. See events.hpp.
#ifdef _WIN32
#include <windows.h>
#include <mutex>
#include <string>

#include "ttmod/events.hpp"
#include "ttmod/gamestate.hpp"
#include "ttmod/log.hpp"
#include "ttmod/plugin_api.h"
#include "events.hpp"

namespace ttmod_win {
namespace {

static std::string g_logpath;
static ttmod::EventBus g_bus;
static ttmod::GameTracker g_tracker;
static std::mutex g_mtx; // subscribe/unsubscribe/tracker/dispatch

static void emit(const std::string& msg) {
    ttmod::Logger log;
    if (log.open(g_logpath)) log.info(msg);
}

} // namespace

void events_init(const char* log_path) {
    g_logpath = log_path ? log_path : "";
}

void dispatch_file_event(const std::string& requested_utf8, const std::string& normalized,
                         const std::string& resolved, const std::string& winner,
                         const std::string& category, bool overridden, bool succeeded) {
    int id = ttmod::EV_FILE_OPEN;
    if (category == "resdesc") id = ttmod::EV_RESDESC_OPEN;
    else if (category == "archive") id = ttmod::EV_ARCHIVE_OPEN;
    {
        std::lock_guard<std::mutex> l(g_mtx);
        if (!g_bus.has(id)) return;
    }
    static volatile LONG s_live = 0;
    if (InterlockedIncrement(&s_live) == 1) emit("events: dispatch live");
    ttmod::FileEvent ev;
    ev.id = id;
    ev.requested = requested_utf8;
    ev.normalized = normalized;
    ev.resolved = resolved;
    ev.winner = winner;
    ev.category = category;
    ev.overridden = overridden;
    ev.succeeded = succeeded;
    ev.thread = (unsigned long)GetCurrentThreadId();
    // Snapshot under lock, invoke WITHOUT it: callbacks re-enter the bridge
    // (get_state/subscribe). Unsubscribe-during-dispatch may still deliver one
    // stale call — documented, harmless (strings are per-dispatch copies).
    std::vector<ttmod::EventBus::Cb> cbs;
    {
        std::lock_guard<std::mutex> l(g_mtx);
        g_tracker.feed(category, normalized);
        cbs = g_bus.snapshot(id);
    }
    for (auto& cb : cbs) cb(ev);
}

int events_subscribe(int event_id, ttmod_event_cb cb, void* ctx) {
    if (event_id < ttmod::EV_FILE_OPEN || event_id > ttmod::EV_ARCHIVE_OPEN || !cb) return -1;
    ttmod_event_cb fn = cb;
    std::lock_guard<std::mutex> l(g_mtx);
    int tok = g_bus.subscribe(event_id, [fn, ctx](const ttmod::FileEvent& e) {
        ttmod_event_file ce;
        ce.id = e.id;
        ce.requested = e.requested.c_str();
        ce.normalized = e.normalized.c_str();
        ce.resolved = e.resolved.c_str();
        ce.winner = e.winner.c_str();
        ce.category = e.category.c_str();
        ce.overridden = e.overridden ? 1 : 0;
        ce.succeeded = e.succeeded ? 1 : 0;
        ce.thread_id = e.thread;
        fn(&ce, ctx);
    });
    char m[96];
    snprintf(m, sizeof m, "events: subscribed token=%d event=%d", tok, event_id);
    emit(m);
    return tok;
}

void events_unsubscribe(int token) {
    std::lock_guard<std::mutex> l(g_mtx);
    g_bus.unsubscribe(token);
}

int events_get_state(ttmod_state* out) {
    if (!out) return -1;
    ttmod::GameSnapshot s;
    {
        std::lock_guard<std::mutex> l(g_mtx);
        s = g_tracker.snapshot();
    }
    out->episode_count = 0;
    for (size_t i = 0; i < s.episodes_seen.size() && i < TTMOD_MAX_EPISODES; ++i)
        out->episodes[out->episode_count++] = s.episodes_seen[i];
    out->archives_opened = s.archives_opened;
    out->resdesc_opened = s.resdesc_opened;
    out->saves_observed = s.saves_observed;
    out->others_opened = s.others_opened;
    strncpy(out->save_dir, s.save_dir.c_str(), sizeof out->save_dir - 1);
    out->save_dir[sizeof out->save_dir - 1] = '\0';
    return 0;
}

// Detach-safe summary: plain snprintf into caller buffer, no IO, no alloc.
int events_state_summary(char* buf, size_t len) {
    if (!buf || !len) return -1;
    ttmod::GameSnapshot s;
    {
        std::lock_guard<std::mutex> l(g_mtx);
        s = g_tracker.snapshot();
    }
    std::string eps;
    for (size_t i = 0; i < s.episodes_seen.size(); ++i) {
        if (i) eps += ',';
        eps += std::to_string(s.episodes_seen[i]);
    }
    return snprintf(buf, len,
                    "state: episodes=[%s] archives=%d resdesc=%d saves=%d others=%d save_dir=%s\r\n",
                    eps.c_str(), s.archives_opened, s.resdesc_opened, s.saves_observed,
                    s.others_opened, s.save_dir.c_str());
}

} // namespace ttmod_win
#endif
