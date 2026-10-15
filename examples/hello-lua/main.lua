-- hello-lua (PR 30 wiring proof): runs on the TTMod-owned Lua 5.4 VM at
-- framework init, in this mod's own environment (writes here are invisible
-- to other mods). Also proves the startup-order fix (review 2026-10-16):
-- ttmod.mods.list() must see the CURRENT plan, not an empty store, because
-- the menu store is populated before script entrypoints run.
ttmod.log.info('hello from TTMod Lua')
local mods = ttmod.mods.list()
ttmod.log.info('mods.list sees ' .. #mods .. ' mods at startup')
