-- hello-lua (PR 30 wiring proof): runs on the TTMod-owned Lua 5.4 VM at
-- framework init, in this mod's own environment (writes here are invisible
-- to other mods). ttmod.log.info lands in logs/ttmod.log as
-- "scripts: hello from TTMod Lua" - the in-game proof line.
ttmod.log.info('hello from TTMod Lua')
hello_marker = 'set in my own env'
