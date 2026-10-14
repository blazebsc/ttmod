#include <cassert>
#include <cstdio>
#include <string>

#include "ttmod/diag.hpp"

int main() {
    using ttmod::DiagEvent;
    using ttmod::DiagKind;
    // Exact wire format: diag|<kind>|<code>|<mod>|<detail>.
    {
        DiagEvent e{DiagKind::Plugin, "rejected-no-native-runtime", "demo.mod",
                    "runtimes declare no native runtime"};
        assert(ttmod::diag_format(e) ==
               "diag|plugin|rejected-no-native-runtime|demo.mod|runtimes declare no native runtime");
    }
    // Empty mod renders as an empty field, not a placeholder.
    {
        DiagEvent e{DiagKind::Startup, "no-mods", "", "no mods discovered, skipping"};
        assert(ttmod::diag_format(e) == "diag|startup|no-mods||no mods discovered, skipping");
    }
    // Newlines are flattened: one event is always one line.
    {
        DiagEvent e{DiagKind::Script, "chunk-error", "demo.mod", "line1\nline2\rline3"};
        assert(ttmod::diag_format(e) == "diag|script|chunk-error|demo.mod|line1 line2 line3");
    }
    // Every kind has a stable short name.
    assert(std::string(ttmod::to_string(DiagKind::Discovery)) == "discovery");
    assert(std::string(ttmod::to_string(DiagKind::Validation)) == "validation");
    assert(std::string(ttmod::to_string(DiagKind::Dependency)) == "dependency");
    assert(std::string(ttmod::to_string(DiagKind::Startup)) == "startup");
    assert(std::string(ttmod::to_string(DiagKind::Script)) == "script");
    assert(std::string(ttmod::to_string(DiagKind::Bridge)) == "bridge");
    assert(std::string(ttmod::to_string(DiagKind::Plugin)) == "plugin");
    printf("diag: format/kinds OK\n");
    return 0;
}
