// Boot-stage timeline diagnostics (permanent, additive, clearly separated).
// Emits "[STAGE <ms>] <msg>" lines (ms since framework InitThread start).
// NEVER modifies existing log lines; core Logger format untouched (tests
// assert exact "[INFO] ..." strings). Windows-only; zero test impact.
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <string>
#include "ttmod/log.hpp"

namespace ttmod_win {

inline DWORD stage_t0() {
    static DWORD t0 = GetTickCount();
    return t0;
}

// Call first in InitThread so ms origin ≈ framework start.
inline void stage_mark_start() { stage_t0(); }

inline void stage(const char* log_path, const char* msg) {
    if (!log_path || !msg) return;
    ttmod::Logger log;
    if (!log.open(log_path)) return;
    char m[256];
    snprintf(m, sizeof m, "[STAGE %lums] %s", (unsigned long)(GetTickCount() - stage_t0()), msg);
    log.info(m);
}

} // namespace ttmod_win
#endif
