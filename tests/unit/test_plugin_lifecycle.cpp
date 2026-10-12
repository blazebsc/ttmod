#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ttmod/plugin_lifecycle.hpp"

int main() {
    using ttmod::PluginRecord;
    using ttmod::PluginState;
    // Fresh record: Discovered, nothing owned.
    {
        PluginRecord rec;
        rec.id = "demo.mod";
        assert(rec.state == PluginState::Discovered);
        assert(rec.tokens.empty());
    }
    // Success path: subscriptions kept, Initialized, nothing to revoke.
    {
        PluginRecord rec;
        rec.state = PluginState::Loaded;
        ttmod::plugin_note_subscribed(rec, 1);
        ttmod::plugin_note_subscribed(rec, 7);
        auto revoke = ttmod::plugin_note_init_result(rec, 0);
        assert(rec.state == PluginState::Initialized);
        assert(revoke.empty() && rec.tokens.size() == 2);
    }
    // Failure path: Failed + owned tokens returned once, record emptied.
    {
        PluginRecord rec;
        rec.state = PluginState::Loaded;
        ttmod::plugin_note_subscribed(rec, 3);
        ttmod::plugin_note_subscribed(rec, 4);
        auto revoke = ttmod::plugin_note_init_result(rec, -3);
        assert(rec.state == PluginState::Failed);
        assert(revoke.size() == 2 && revoke[0] == 3 && revoke[1] == 4);
        assert(rec.tokens.empty());
        // Idempotent: repeating the failure revokes nothing more.
        assert(ttmod::plugin_note_init_result(rec, -3).empty());
        // Terminal: success after failure does NOT resurrect.
        assert(ttmod::plugin_note_init_result(rec, 0).empty());
        assert(rec.state == PluginState::Failed);
    }
    // Refused/invalid tokens are never owned.
    {
        PluginRecord rec;
        ttmod::plugin_note_subscribed(rec, -1);
        ttmod::plugin_note_subscribed(rec, 0);
        assert(rec.tokens.empty());
        assert(ttmod::plugin_note_init_result(rec, -1).empty());
        assert(rec.state == PluginState::Failed);
    }
    printf("plugin-lifecycle: states/tokens/revocation OK\n");
    return 0;
}
