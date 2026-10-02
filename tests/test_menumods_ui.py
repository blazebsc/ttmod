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
-- Clone_Find returns a CLONE AGENT (the engine's real return value), so the
-- fake carries `of` through; returning the widget table would not model it.
function Clone_Find(b, what) return {of = (type(b) == 'table' and b.of or '?'), what = what, clone = what} end
function AgentSetProperty(a, k, v) rec('setprop', tostring(a and a.of), k, tostring(v))
  if k == nil then error('nil property') end
  _props[(tostring(a and a.of) or '?') .. '/' .. (a and a.clone or '') .. '/' .. k] = tostring(v) end
-- Only 'Color' is a real engine property in this stub; anything else is
-- accepted-then-ignored (the silent-failure case the read-back guards).
-- Agent identity = widget id .. '/' .. clone name, so the getter sees the same
-- key the setter wrote.
local function AgentGetPropertyImpl(a, k)
  if k == nil then return nil end
  if k ~= 'Color' then return nil end
  return _props[(tostring(a and a.of) or '?') .. '/' .. (a and a.clone or '') .. '/' .. k]
end
_props = {}
AgentGetProperty = AgentGetPropertyImpl
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
    { key = 'accent', type = 'color', label = 'Accent', value = '#00E000' },
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
-- color row opens palette page 1: 6 swatches + Next + Back. The engine renders
-- a FIXED number of ListButton rows per menu and rows past that are added but
-- render blank (in-game: 17 blank boxes), so the palette is PAGINATED and a
-- page must stay small.
setter_log = {}
calls = {}
Menu_Mods_Adjust('demo.config', 'accent')
assert(nrec('add|sw_1|') == 1 and nrec('add|sw_6|') == 1, 'page 1 swatches')
assert(nrec('add|sw_7|') == 0 and nrec('add|sw_16|') == 0, 'page 1 stops at 6')
assert(nrec('add|prevpage|') == 0, 'no Previous on first page')
assert(nrec('add|nextpage|') == 1, 'Next on first page')
assert(nrec('add|back|') == 1, 'palette back row')
assert(#setter_log == 0, 'opening palette writes nothing')
-- the current swatch (#00E000 = swatch 10) carries the ' *' mark, but only on
-- the page that holds it. Runs BEFORE the pick below mutates the fixture.
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 2)
local marked = false
for _, c in ipairs(calls) do
  if c:find('setprop') and c:find('#00E000 *', 1, true) then marked = true end
end
assert(marked, 'current swatch marked on page 2')
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 1)
for _, c in ipairs(calls) do
  if c:find('setprop') and c:find('#00E000 *', 1, true) then error('marked off its page') end
end
-- last page: Previous, no Next, and it covers the tail of the palette
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 3)
assert(nrec('add|sw_13|') == 1 and nrec('add|sw_16|') == 1, 'page 3 tail')
assert(nrec('add|sw_17|') == 0, 'no row past the palette')
assert(nrec('add|prevpage|') == 1 and nrec('add|nextpage|') == 0, 'Previous on last page')
-- every swatch row painted with 'Color' exactly once (the only property the
-- stub reads back: unknown keys are accepted-then-ignored, the silent-failure
-- case the read-back guard handles; 'Tint Color' must NOT be attempted after
-- 'Color' proved itself)
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 1)
for i = 1, 6 do
  local row = '|sw_' .. i .. '|'
  local n, bad = 0, nil
  for _, c in ipairs(calls) do
    if c:sub(1, 7) == 'setprop' and c:find(row, 1, true) then
      if c:find('|Color|#', 1, true) then n = n + 1
      elseif not c:find('Text String', 1, true) then bad = c end
    end
  end
  assert(n == 1, 'swatch ' .. i .. ' painted once, got ' .. n)
  assert(bad == nil, 'swatch ' .. i .. ' painted only Color, got ' .. tostring(bad))
end
-- picking a swatch writes it and returns to details
setter_log = {}
calls = {}
Menu_Mods_SetColor('demo.config', 'accent', '#0080FF')
assert(setter_log[1] == 'set:demo.config.accent=#0080FF', 'swatch write, got ' .. tostring(setter_log[1]))
assert(nrec('pop') == 1 and nrec('create|ui_menu_options') == 1, 'pick pops to details')
-- garbage hex is refused without touching config
setter_log = {}
Menu_Mods_SetColor('demo.config', 'accent', 'blue')
assert(#setter_log == 0, 'non-hex refused')
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

# The GAME runs Lua 5.1 (setmetatable/table.unpack are 5.2-only and are nil
# there - a setmetatable in menumods_ui.lua killed every menu screen in-game
# while this suite stayed green on 5.2). So the same proof runs on BOTH.
for interp in ("lua5_1", "lua5_2"):
    r = subprocess.run(
        ["nix-shell", "-p", interp, "--run", "lua -e " + "'" + DRIVER.replace("'", "'\\''") + "'"],
        cwd=ROOT, capture_output=True, text=True, timeout=300)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0 or "menumods-ui: screen logic OK" not in r.stdout:
        print(f"FAIL: menumods_ui.lua screen logic on {interp}")
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
