#!/usr/bin/env python3
"""Dynamic Menu_Add wrapper test on stock Lua 5.2 (system interpreter).
Proves: one-shot install, exact arg/return passthrough incl. varargs,
append fires only after the main-menu exit row, other menus unaffected,
re-offer no-op."""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HDR = os.path.join(ROOT, "loader", "windows", "menu", "menu_bridge.hpp")
with open(HDR, encoding="utf-8") as f:
    hsrc = f.read()
i = hsrc.index("kMenuAddWrapChunk")
j = hsrc.index(";", i)
CHUNK = "".join(re.findall(r'"([^"]*)"', hsrc[i:j]))
assert "ttmod_orig_Add" in CHUNK, CHUNK
# Static pins (moved from tests/test_lua_bridge.cpp with the chunk):
# one-shot guard, guarded Mods callback, pcall-protected label overwrite.
for needle in ("ttmod_appended", "Menu_Mods()", "__b.agent or __b, 'label'",
               "Text String', 'Mods'", "pcall", "mods-appended",
               "if Menu_Mods then", "ttmod_appended = nil"):
    assert needle in CHUNK, needle
assert "Menu_Main_Exit" not in CHUNK, "id-only trigger"
assert "table.pack" not in CHUNK, CHUNK
assert "tostring(" not in CHUNK, CHUNK
# Every widget must reach the theming hook, so one config colour re-themes the
# game's own menus too - not just the framework's Mods screens.
for needle in ("TTMOD_THEME_WIDGET", "pcall(TTMOD_THEME_WIDGET, __r)"):
    assert needle in CHUNK, needle
# A "--" comment inside the literal would swallow the rest of the chunk: the
# fragments concatenate with no newlines between them.
assert "--" not in CHUNK, "comment in chunk literal would comment out the rest"

DRIVER = r"""
adds = {}
append_log = {}
themed = {}
function TTMOD_THEME_WIDGET(w) themed[#themed+1] = (w and w.id) or '?' end
function Menu_Main_AppendLog(s) append_log[#append_log+1] = s end
ListButton = {}
propset = {}
function Clone_Find(b, what) return {btn = b and b.id, what = what} end
function AgentSetProperty(a, k, v) propset[#propset+1] = (a and a.btn or '?') .. '|' .. k .. '|' .. v end
-- original records full arg list incl varargs, returns the button agent
function Menu_Add(widget, id, label, cb, ...)
  local n = select('#', ...)
  adds[#adds+1] = id .. '|' .. label .. '|' .. cb .. '|vx' .. n
  return {id = id}
end
""" + CHUNK + r"""
CDCB = 'if Menu_Main_AppendLog ~= nil then Menu_Main_AppendLog([[mods-clicked]]) end if Menu_Mods then Menu_Mods() end'
assert(type(Menu_Add) == 'function' and ttmod_orig_Add ~= nil, 'wrapped')
-- other menu rows pass through untouched, return preserved (single agent)
local a = Menu_Add(ListButton, 'settings', 'label_settings', 'Menu_Options()', 'xtra')
assert(type(a) == 'table' and a.id == 'settings', 'passthrough return')
assert(adds[1] == 'settings|label_settings|Menu_Options()|vx1', 'passthrough args')
assert(#adds == 1, 'no append yet')
-- trace counted without tostring
assert(append_log[1] == 'row-settings', 'census settings')
-- main-menu exit row triggers the single Mods append + literal overwrite
-- (append lands first, exit row second — Mods sits above Exit Game)
Menu_Add(ListButton, 'exit', 'label_exit', 'Menu_Main_Exit()')
assert(#adds == 3, 'append + orig exit, got ' .. #adds)
assert(adds[2] == 'mods|label_help|' .. CDCB .. '|vx0', 'append shape')
assert(adds[3] == 'exit|label_exit|Menu_Main_Exit()|vx0', 'orig exit last')
assert(propset[1] == 'mods|Text String|Mods', 'literal overwrite, got ' .. tostring(propset[1]))
assert(append_log[#append_log] == 'mods-appended', 'append reported')
-- second exit row (same build): no re-append
local n1 = #adds
Menu_Add(ListButton, 'exit', 'label_exit', 'Menu_Main_Exit()')
assert(#adds == n1 + 1, 'no re-append')
-- menu revisit: play row re-arms the one-shot guard, exit appends again
Menu_Add(ListButton, 'play', 'label_play', 'Menu_Main_Play()')
Menu_Add(ListButton, 'exit', 'label_exit', 'Menu_Main_Exit()')
assert(adds[#adds-1] == 'mods|label_help|' .. CDCB .. '|vx0', 'revisit re-append')
assert(append_log[#append_log] == 'mods-appended', 'revisit reported')
-- exit with an unexpected callback still appends: live rows carry a cb
-- string that differs from the researched one, so id is the trigger
Menu_Add(ListButton, 'play', 'label_play', 'Menu_Main_Play()')
Menu_Add(ListButton, 'exit', 'label_exit', 'UnexpectedCb()')
assert(adds[#adds-1] == 'mods|label_help|' .. CDCB .. '|vx0', 'cb-insensitive append')
assert(append_log[#append_log-1] == 'UnexpectedCb()', 'exit cb reported')
-- a THROWING Clone_Find must not reach the game (the live menu-kill bug):
-- overwrite is swallowed, exit row still added, wrapper still callable
local real_find = Clone_Find
function Clone_Find() error('boom') end
Menu_Add(ListButton, 'play', 'label_play', 'Menu_Main_Play()')
Menu_Add(ListButton, 'exit', 'label_exit', 'Menu_Main_Exit()')
assert(adds[#adds-1]:sub(1,4) == 'mods' and adds[#adds]:sub(1,4) == 'exit', 'throw-proof append')
assert(append_log[#append_log] == 'mods-appended', 'survived throw')
Clone_Find = real_find
-- non-exit rows never trigger
local _n = #adds
Menu_Add(ListButton, 'store', 'label_store', 'Menu_Store()')
assert(#adds == _n + 1, 'plain row clean')
-- EVERY widget (not just the Mods row) reaches the theming hook, so a colour
-- set in a mod's config re-themes the game's own menus as well as ours.
local nt = #themed
assert(nt > 0, 'themed widgets recorded')
assert(themed[#themed] == 'store', 'every widget themed, last=' .. tostring(themed[#themed]))
-- the hook is pcall'd: a throwing themer must not reach the game
function TTMOD_THEME_WIDGET() error('theme boom') end
Menu_Add(ListButton, 'help', 'label_help', 'Menu_Help()')
assert(adds[#adds] == 'help|label_help|Menu_Help()|vx0', 'throwing themer survived')
"""

import lua_runner

r = lua_runner.run("5.2", DRIVER)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
# NOTE: no print() marker (print needs tostring, nilled here); rc==0 means
# every assert passed.
if r.returncode != 0:
    print("FAIL: Menu_Add wrapper semantics")
    sys.exit(1)
print("menuadd-wrap: deterministic proof passed")
