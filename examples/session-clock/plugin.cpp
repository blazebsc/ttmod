// session-clock example plugin: logs session duration via get_state/events.
// Records its init tick, subscribes to open events, and every 100 files
// snapshots get_state and logs elapsed wall time + lifecycle counters.
// Stable ABI v2+v3 only. Callbacks run on the game thread: non-blocking.
#include "ttmod/plugin_api.h"
#include <windows.h>
#include <cstdio>

namespace {

const ttmod_host* g_host = nullptr;
static volatile LONG g_total = 0;
static ULONGLONG g_start_ms = 0;

static void say(const char* msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

static void on_ev(const ttmod_event_file* ev, void* ctx) {
    (void)ev;
    (void)ctx;
    LONG total = InterlockedIncrement(&g_total);
    if (total % 100 != 0) return;
    unsigned long secs = (unsigned long)((GetTickCount64() - g_start_ms) / 1000);
    if (g_host->api_version >= 3 && g_host->get_state) {
        ttmod_state st{};
        if (g_host->get_state(&st) == 0) {
            char m[256];
            snprintf(m, sizeof m, "session-clock: t=%lus files=%ld eps=%d archives=%d saves=%d", secs,
                     (long)total, st.episode_count, st.archives_opened, st.saves_observed);
            say(m);
            return;
        }
    }
    char m[128];
    snprintf(m, sizeof m, "session-clock: t=%lus files=%ld", secs, (long)total);
    say(m);
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 3 || !host->subscribe || !host->get_state) {
        host->log("session-clock: needs host api >= 3, staying unloaded");
        return -2;
    }
    g_host = host;
    g_start_ms = GetTickCount64();
    int t1 = host->subscribe(TTMOD_EVENT_FILE_OPEN, on_ev, nullptr);
    int t2 = host->subscribe(TTMOD_EVENT_RESDESC_OPEN, on_ev, nullptr);
    int t3 = host->subscribe(TTMOD_EVENT_ARCHIVE_OPEN, on_ev, nullptr);
    char m[160];
    snprintf(m, sizeof m, "session-clock: started, subscribed file=%d resdesc=%d archive=%d", t1, t2, t3);
    host->log(m);
    return (t1 > 0 && t2 > 0 && t3 > 0) ? 0 : -3;
}
