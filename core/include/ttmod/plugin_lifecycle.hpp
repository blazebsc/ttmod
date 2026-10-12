#pragma once
// Native plugin lifetime model (roadmap PR 21).
//
// States: Discovered -> Loaded (LoadLibrary ok) -> Initialized (init
// returned 0) / Failed (init returned nonzero). A missing export stays
// Loaded: kept loaded, never inited. Every state is terminal - nothing
// unloads, nothing retries, nothing re-inits (explicit non-support:
// unloading live native code with outstanding event callbacks is
// unsound; disable-at-runtime requires a restart).
//
// The loader initializes plugins sequentially, one at a time, over the
// ModPlan dependency-ordered set. Event callbacks are valid from
// subscribe() until process end, EXCEPT a failed init: the loader
// unsubscribes that plugin's tokens synchronously right after the
// nonzero return, before the next plugin initializes. A dispatch on the
// init thread cannot interleave; a cross-thread dispatch can, so
// revocation is prompt, not atomic - stated here so nobody assumes more.
//
// Token ownership needs the loader's help: subscribe is called BY the
// plugin through its host struct, so the loader wraps subscribe per
// plugin and records granted tokens into that plugin's record (see
// plugins.cpp). This header only decides; it performs no I/O.
#include <string>
#include <vector>

namespace ttmod {

enum class PluginState { Discovered, Loaded, Initialized, Failed };

struct PluginRecord {
    std::string id;
    PluginState state = PluginState::Discovered;
    std::vector<int> tokens; // event subscriptions owned by this plugin
};

// Record a subscription. Only positive tokens are owned; -1 (refused)
// and 0 (never issued) are ignored.
inline void plugin_note_subscribed(PluginRecord& rec, int token) {
    if (token > 0) rec.tokens.push_back(token);
}

// Record init's return code. 0 -> Initialized, nothing to revoke.
// Nonzero -> Failed + the owned tokens, so the caller can unsubscribe
// them. Terminal both ways: a Failed record stays Failed even if told
// about a later success (nothing retries, nothing re-inits), and repeats
// return empty (revocation happens once).
inline std::vector<int> plugin_note_init_result(PluginRecord& rec, int rc) {
    if (rec.state == PluginState::Failed) return {};
    if (rc == 0) {
        rec.state = PluginState::Initialized;
        return {};
    }
    rec.state = PluginState::Failed;
    std::vector<int> out;
    out.swap(rec.tokens);
    return out;
}

} // namespace ttmod
