-- hello-telltale (PR 32 proof): runs on the game's OWN Lua states, not the
-- TTMod VM. The loader queues this at init; it executes on the game script
-- thread at the next script load, plus the menu-state replay - once per Lua
-- world, with separate globals each time. Hence the idempotence guard:
-- double side effects are the author's bug, not the loader's.
-- No return channel exists; debug via ttmod_menu_log (menu states only -
-- the engine state never registers it, so the call is guarded).
if _TTMOD_HELLO_TELLTALE == nil then
    _TTMOD_HELLO_TELLTALE = true
    if ttmod_menu_log then
        ttmod_menu_log('hello-telltale: running on a game Lua state')
    end
end
