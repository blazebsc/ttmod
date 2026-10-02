// event-log example plugin (M7): subscribes to file/resdesc/archive open
// events and logs a bounded sample plus per-category counts. Demonstrates the
// event path without flooding logs. Callbacks run on the game thread that
// opened the file: non-blocking, no framework calls except host->log.
#include "ttmod/plugin_api.h"
#include <windows.h>
#include <cstdio>

namespace {

const ttmod_host* g_host = nullptr;
static volatile LONG g_counts[4] = {0, 0, 0, 0};

static void say(const char* msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

static void on_ev(const ttmod_event_file* ev, void* ctx) {
    (void)ctx;
    if (!ev || ev->id < 1 || ev->id > 3) return;
    LONG n = InterlockedIncrement(&g_counts[ev->id]);
    LONG total = g_counts[1] + g_counts[2] + g_counts[3];
    if (n <= 3 || total % 100 == 0) {
        char m[512];
        snprintf(m, sizeof m, "event-log: %s #%ld ok=%d ov=%d [%s]", ev->category, (long)n,
                 ev->succeeded, ev->overridden, ev->requested);
        say(m);
    }
    // M8: exercise the read-only state query on the same cadence.
    if (total % 100 == 0 && g_host->api_version >= 3 && g_host->get_state) {
        ttmod_state st{};
        if (g_host->get_state(&st) == 0) {
            char m[256];
            snprintf(m, sizeof m, "event-log: state eps=%d archives=%d resdesc=%d saves=%d",
                     st.episode_count, st.archives_opened, st.resdesc_opened, st.saves_observed);
            say(m);
        }
    }
}

} // namespace

extern "C" __declspec(dllexport) int ttmod_plugin_init(const ttmod_host* host) {
    if (!host || !host->log) return -1;
    if (host->api_version < 2 || !host->subscribe) {
        host->log("event-log: needs host api >= 2, staying unloaded");
        return -2;
    }
    g_host = host;
    int t1 = host->subscribe(TTMOD_EVENT_FILE_OPEN, on_ev, nullptr);
    int t2 = host->subscribe(TTMOD_EVENT_RESDESC_OPEN, on_ev, nullptr);
    int t3 = host->subscribe(TTMOD_EVENT_ARCHIVE_OPEN, on_ev, nullptr);
    char m[160];
    snprintf(m, sizeof m, "event-log: subscribed file=%d resdesc=%d archive=%d", t1, t2, t3);
    host->log(m);
    // v5 demo: queue a Lua chunk; the bridge runs it on the game's script
    // thread at the next script load. ttmod_menu_log is registered on every
    // captured state, so this line lands in ttmod.log as live proof.
    if (host->api_version >= 5 && host->queue_ui_chunk) {
        int q = host->queue_ui_chunk(
            "if ttmod_menu_log then ttmod_menu_log('event-log: Lua chunk ran on the script thread') end");
        host->log(q == 0 ? "event-log: ui chunk queued" : "event-log: ui chunk dropped");
    }
    return (t1 > 0 && t2 > 0 && t3 > 0) ? 0 : -3;
}
