#!/usr/bin/env python3
"""Deterministic test of the REAL menumods_ui.lua on stock Lua 5.2.
Stubs record native calls; a fake backend serves ttmod_menu and records
setter calls. Proves screen logic (not rendering): list rows, select,
toggle, adjust-per-type, back/pop sequencing."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "loader", "windows", "menumods_ui.lua")

STUBS = r"""
calls = {}
function rec(...) local t = {} for i = 1, select('#', ...) do t[#t+1] = tostring(select(i, ...)) end calls[#calls+1] = table.concat(t, '|') end
ListMenu, Header, ListButton = {}, {}, {}
function Menu_Create(w, scene) rec('create', scene) return {scene = scene} end
function Menu_Add(w, id, label, cb) rec('add', tostring(id), tostring(label), tostring(cb)) return {id = id, agent = {of = tostring(id)}} end
function Menu_Push(m) rec('push') if m.Populate then m:Populate() end end
function Menu_Pop() rec('pop') end
function Clone_Find(b, what) return {of = (type(b) == 'table' and b.of or '?'), what = what} end
function AgentSetProperty(a, k, v) rec('setprop', tostring(a and a.of), k, tostring(v)) end
function EscapeText2(s) return tostring(s) end
function WidgetInputHandler_EnableInput(b) rec('input', tostring(b)) end
function Menu_OpenTextEntryBox(init, prompt) rec('textbox', tostring(init), tostring(prompt)) return 'Typed!', true end
setter_log = {}
function ttmod_menu_set_enabled(id, v) setter_log[#setter_log+1] = 'en:'..id..'='..v
  for _, m in ipairs(ttmod_menu.mods) do if m.id == id then m.enabled = (v == '1') end end end
function ttmod_menu_set_value(id, k, v) setter_log[#setter_log+1] = 'set:'..id..'.'..k..'='..v
  for _, m in ipairs(ttmod_menu.mods) do if m.id == id then for _, c in ipairs(m.config) do
    if c.key == k then
      if c.type == 'bool' then c.value = (v == '1')
      elseif c.type == 'int' then c.value = tonumber(v)
      elseif c.type == 'enum' then c.value = v
      else c.value = v end end end end end end
function ttmod_menu_log(s) rec('log', tostring(s)) end
function ttmod_menu_refresh() end
ttmod_menu = { seq = 1, mods = {
  { id = 'demo.config', name = 'Demo', version = '1.0', enabled = true, config = {
    { key = 'fancy', type = 'bool', label = 'Fancy', value = true },
    { key = 'level', type = 'int', label = 'Level', value = 3, min = 1, max = 3, step = 1 },
    { key = 'mode', type = 'enum', label = 'Mode', value = 'a', options = {'a', 'b'} },
    { key = 'greet', type = 'string', label = 'Greet', value = 'Hi' },
  } },
  { id = 'plain.mod', name = 'Plain', version = '0.1', enabled = false },
} }
"""

DRIVER = STUBS + open(UI, encoding="utf-8").read() + r"""
function nrec(p) local n = 0 for _, c in ipairs(calls) do if c:sub(1, #p) == p then n = n + 1 end end return n end
-- list screen
Menu_Mods()
assert(nrec('create|ui_menu_options') == 1, 'one menu created')
assert(nrec('add|mod_demo.config') == 1, 'demo row')
assert(nrec('add|mod_plain.mod') == 1, 'plain row')
assert(nrec('add|back') == 1, 'back row')
assert(nrec('push') == 1, 'pushed once')
-- label literal overwrite happened for the demo row (name+version+ON)
local found = false
for _, c in ipairs(calls) do if c:find('Demo', 1, true) and c:find('ON', 1, true) then found = true end end
assert(found, 'demo literal label with state')
-- select -> details (version, toggle, 3 config rows, hint, back)
calls = {}
Menu_Mods_Select('demo.config')
assert(nrec('add|enabled') == 1, 'enabled row')
assert(nrec('add|cfg_fancy') == 1 and nrec('add|cfg_level') == 1 and nrec('add|cfg_mode') == 1, 'config rows')
assert(nrec('add|hint') == 1, 'restart hint')
-- toggle flips + rebuilds (pop then fresh select = new create)
calls = {}
Menu_Mods_Toggle('demo.config')
assert(setter_log[1] == 'en:demo.config=0', 'toggle setter, got ' .. tostring(setter_log[1]))
assert(nrec('pop') == 1 and nrec('create|ui_menu_options') == 1, 'pop+rebuild')
-- bool adjust
setter_log = {}
Menu_Mods_Adjust('demo.config', 'fancy')
assert(setter_log[1] == 'set:demo.config.fancy=0', 'bool adjust')
-- int wrap (3+1 > max 3 -> min 1)
setter_log = {}
Menu_Mods_Adjust('demo.config', 'level')
assert(setter_log[1] == 'set:demo.config.level=1', 'int wrap, got ' .. tostring(setter_log[1]))
-- enum cycle a -> b
setter_log = {}
Menu_Mods_Adjust('demo.config', 'mode')
assert(setter_log[1] == 'set:demo.config.mode=b', 'enum cycle')
-- string row opens the native text box; confirm writes the typed text
setter_log = {}
calls = {}
Menu_Mods_Adjust('demo.config', 'greet')
assert(nrec('textbox|Hi|Greet') == 1, 'native textbox opened with current+label')
assert(setter_log[1] == 'set:demo.config.greet=Typed!', 'typed text saved')
assert(nrec('input|false') == 1 and nrec('input|true') == 1, 'input disabled around dialog')
-- unknown mod is a safe no-op (trace logs don't count)
calls = {}
Menu_Mods_Select('nope')
do local n = 0 for _, c in ipairs(calls) do if c:sub(1, 4) ~= 'log|' then n = n + 1 end end
  assert(n == 0, 'no-op on unknown') end
-- empty registry
calls = {}
ttmod_menu = { seq = 2, mods = {} }
Menu_Mods_Show()
assert(nrec('add|nomods') == 1, 'empty row')
print('menumods-ui: screen logic OK')
"""

r = subprocess.run(
    ["nix-shell", "-p", "lua5_2", "--run", "lua -e " + "'" + DRIVER.replace("'", "'\\''") + "'"],
    cwd=ROOT, capture_output=True, text=True, timeout=300)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
if r.returncode != 0 or "menumods-ui: screen logic OK" not in r.stdout:
    print("FAIL: menumods_ui.lua screen logic")
    sys.exit(1)
# the C++-built literal must PARSE on stock 5.2 (needs test_modconfig run first)
r2 = subprocess.run(
    ["nix-shell", "-p", "lua5_2", "--run", "lua /tmp/opencode/menu_literal_check.lua"],
    cwd=ROOT, capture_output=True, text=True, timeout=300)
sys.stdout.write(r2.stdout)
sys.stderr.write(r2.stderr)
if r2.returncode != 0:
    print("FAIL: build_menu_literal output does not parse")
    sys.exit(1)
print("menumods-ui: deterministic proof passed")
