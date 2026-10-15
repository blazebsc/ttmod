-- hello-luau (PR 31 wiring proof): runs on the TTMod-owned Luau VM at
-- framework init, in its own environment. Same binding surface as the Lua
-- backend by construction. Luau syntax demo below (compound assignment);
-- the log line is the in-game proof: "scripts: hello from Luau".
ttmod.log.info('hello from Luau')
local n = 0
n += 1 -- Luau compound assignment; the Lua backend would reject this file
assert(n == 1)
