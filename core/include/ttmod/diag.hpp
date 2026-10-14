#pragma once
// Structured runtime diagnostics (roadmap PR 24).
//
// One line per event, countable by category: "diag|<kind>|<code>|<mod>|<detail>".
// Codes reuse the historical log vocabulary (rejected, initialized, failed)
// so old grep habits keep working. Detail is human text with newlines
// flattened (single-line invariant for offline tooling).
//
// Hook-path rule: emitting costs what the sink costs (today: a log-file
// open/write/close per line), so hot paths must already be capped before
// calling - see the scol/loadresource caps. This header adds no sink and
// no thread; callers own where lines go, exactly like before.
#include <string>

namespace ttmod {

enum class DiagKind {
    Discovery,   // found/lost mods and packages
    Validation,  // manifest/config/schema rejections
    Dependency,  // conflicts, cycles, blocked sets
    Startup,     // runtime/mod init, lifecycle transitions
    Script,      // TTMod VM chunk errors, budgets, bindings
    Bridge,      // game-Lua bridge observations and failures
    Plugin,      // native plugin load/init/revoke
};

inline const char* to_string(DiagKind k) {
    switch (k) {
        case DiagKind::Discovery:
            return "discovery";
        case DiagKind::Validation:
            return "validation";
        case DiagKind::Dependency:
            return "dependency";
        case DiagKind::Startup:
            return "startup";
        case DiagKind::Script:
            return "script";
        case DiagKind::Bridge:
            return "bridge";
        default:
            return "plugin";
    }
}

struct DiagEvent {
    DiagKind kind;
    std::string code;    // stable kebab id, e.g. "rejected-no-native-runtime"
    std::string mod;     // owning mod id, or empty
    std::string detail;  // human text; newlines are flattened at format time
};

inline std::string diag_format(const DiagEvent& e) {
    std::string d = e.detail;
    for (char& c : d)
        if (c == '\n' || c == '\r') c = ' ';
    std::string out("diag|");
    out += to_string(e.kind);
    out += '|';
    out += e.code;
    out += '|';
    out += e.mod;
    out += '|';
    out += d;
    return out;
}

} // namespace ttmod
