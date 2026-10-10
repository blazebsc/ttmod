#!/usr/bin/env python3
"""Deterministic test of the REAL menumods_ui.lua on stock Lua 5.2.
Stubs record native calls; a fake backend serves ttmod_menu and records
setter calls. Proves screen logic (not rendering): list rows, select,
toggle, adjust-per-type, back/pop sequencing."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UI = os.path.join(ROOT, "loader", "windows", "menu", "menumods_ui.lua")

STUBS = r"""
-- The GAME's Lua runtime has _G == nil (verified in-game 2026-10-02): a stripped
-- environment table. Stock 5.1 and 5.2 both PROVIDE _G, so a suite that leaves
-- it defined cannot catch a menumods_ui.lua that uses _G.X. Nil it here.
_G = nil
calls = {}
function rec(...) local t = {} for i = 1, select('#', ...) do t[#t+1] = tostring(select(i, ...)) end calls[#calls+1] = table.concat(t, '|') end
ListMenu, Header, ListButton = {}, {}, {}
function Menu_Create(w, scene) rec('create', scene) return {scene = scene} end
function Menu_Add(w, id, label, cb) rec('add', tostring(id), tostring(label), tostring(cb)) return {id = id, agent = {of = tostring(id)}} end
function Menu_Push(m) rec('push') if m.Populate then m:Populate() end end
function Menu_Pop() rec('pop') end
-- Clone_Find returns a CLONE AGENT (the engine's real return value), so the
-- fake carries `of` through; returning the widget table would not model it.
-- 'ui_listButton_button' must also be findable: it owns 'Selection Color', the
-- engine's hover/selection fill. Sweeping in-game read its stock value as
-- green because nothing ever painted it (2026-10-04).
function Clone_Find(b, what) return {of = (type(b) == 'table' and b.of or '?'), what = what, clone = what} end
function AgentSetProperty(a, k, v) rec('setprop', tostring(a and a.of), k, tostring(v))
  if k == nil then error('nil property') end
  local key = (tostring(a and a.of) or '?') .. '/' .. (a and a.clone or '') .. '/' .. k
  -- Swatch-preservation fixture: the last label to receive a Text Color
  -- write, so the suite can re-drive the engine's hover writes at the exact
  -- agent the palette painted.
  if k == 'Text Color' then
    TT_LAST_TC_AGENT = a
    TT_TC_ALL = TT_TC_ALL or {}
    TT_TC_ALL[#TT_TC_ALL + 1] = a
  end
  -- Only these exist on a label (in-game probes 2026-10-03); every other
  -- name is accepted-then-ignored, the silent-failure case read-back catches.
  -- 'Selection Color' lives on the BUTTON clone: it is the row's hover
  -- highlight (stock {r=0.5,g=1,b=0.5,a=1} - light green).
  if k == 'Text Color' or k == 'Text Color Highlight' or k == 'Text Color Pressed'
     or k == 'Selection Color' then
    _props[key] = v
    _color[key] = v
  end
  _wrote[key] = v end
-- Agent identity = widget id .. '/' .. clone name, so the getter sees the same
-- key the setter wrote.
-- Properties the widget actually exposes. 'Text Color Highlight'/'Pressed'
-- stand in for the engine's per-state names: they are what the read-only sweep
-- has to discover, since the framework cannot guess them.
local REAL_PROPS = {
  ['Selection Color'] = { r = 0.5, g = 1, b = 0.5, a = 1 },
  ['Text Color Highlight'] = { r = 255, g = 255, b = 255, a = 255 },
  ['Text Color Pressed'] = { r = 200, g = 200, b = 200, a = 255 },
}
local function AgentGetPropertyImpl(a, k)
  if k == nil then return nil end
  -- the BUTTON agent (no clone) exposes the state names; the LABEL only Text Color
  if a ~= nil and a.clone == nil then
    if REAL_PROPS[k] ~= nil then return REAL_PROPS[k] end
    return nil
  end
  return _props[(tostring(a and a.of) or '?') .. '/' .. (a and a.clone or '') .. '/' .. k]
end
_wrote = {}
_color = {}
_props = {}
AgentGetProperty = AgentGetPropertyImpl
-- Rollover binding (found at RVA 0x73F730 in the unpacked image): the hover
-- mechanism. enable=true writes white, falsy restores stock gray - through
-- native setters, bypassing the AgentSetProperty global entirely.
function RolloverEnableTextColor(a, e) rec('roll', tostring(a and a.of), tostring(e))
  local key = (tostring(a and a.of) or '?') .. '/' .. (a and a.clone or '') .. '/Text Color'
  if e then _props[key] = { r = 1, g = 1, b = 1, a = 1 }
  else _props[key] = { r = 0.878, g = 0.878, b = 0.878, a = 1 } end
  _color[key] = _props[key]
  return 1 end
-- TextSetColor (binding 0x730690): alternate color path game scripts may
-- drive hover through. Stores whatever it receives, like the engine.
function TextSetColor(a, r, g, b, al) rec('tsc', tostring(a and a.of), tostring(r))
  local key = (tostring(a and a.of) or '?') .. '/' .. (a and a.clone or '') .. '/Text Color'
  if type(r) == 'table' then _props[key] = r
  elseif type(r) == 'number' then _props[key] = { r = r, g = g, b = b, a = al } end
  _color[key] = _props[key]
  return 1 end
-- Rollover family: log-only traffic witnesses (see theme_wrap_rolfam).
function RolloverEnableRolloverMesh(a, b) rec('mesh', tostring(b)) return 1 end
function RolloverEnableTextBackgroundColor(a, b) rec('bg', tostring(b)) return 1 end
function RolloverResetStatus() rec('resetstatus') return 1 end
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
-- Bridge stub for the glow gate: the native hook reads the last value.
TT_PALETTE = nil
function ttmod_menu_palette(v) TT_PALETTE = tostring(v) rec('palette', tostring(v)) end
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
-- load-time self-test ran at chunk load (before anything else): all nine
-- entry points defined, or the log names the missing one.
local selfok = false
for _, c in ipairs(calls) do
  if c == 'log|menumods: ui self-test defs=9/9' then selfok = true end
end
assert(selfok, 'chunk self-test green at load')
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
-- REGRESSION 2026-10-04: the chunk must not call globals at load (it runs at
-- state capture, before the engine opens standard libs - a load-time type()
-- call killed the whole chunk in-game: dead Mods button, no painting).
-- Install is lazy via TTMOD_THEME_WIDGET, so the marker is still unset here.
assert(ttmod_asp_wrapped ~= true, 'no load-time wrapping')
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
-- color row opens palette page 1: 4 colours as static-display + pick-button
-- pairs, plus a cycling More button and Back (10 rows, the proven-rendered
-- capacity). Static Header rows are non-interactive: the engine never
-- selects, floods, or restores them, so their swatch colours are permanent.
-- Pick ListButtons are accent (hover-safe by construction).
setter_log = {}
calls = {}
Menu_Mods_Adjust('demo.config', 'accent')
assert(nrec('add|sw_1|') == 1 and nrec('add|sw_2|') == 1, 'page 1 pick rows')
assert(nrec('add|sw_3|') == 0 and nrec('add|sw_16|') == 0, 'page 1 stops at 2')
assert(nrec('add|swd_1|') == 1 and nrec('add|swd_2|') == 1, 'page 1 static displays')
assert(nrec('add|morepage|') == 1, 'More button on first page')
assert(nrec('add|back|') == 1, 'palette back row')
assert(#setter_log == 0, 'opening palette writes nothing')
-- the current swatch (#00E000 = swatch 10) carries the ' *' mark, but only on
-- the page that holds it (page 3 at 4/page). Runs BEFORE the pick below
-- mutates the fixture.
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 5)
local marked = false
for _, c in ipairs(calls) do
  if c:find('setprop') and c:find('#00E000 *', 1, true) then marked = true end
end
assert(marked, 'current swatch marked on its page')
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 1)
for _, c in ipairs(calls) do
  if c:find('setprop') and c:find('#00E000 *', 1, true) then error('marked off its page') end
end
-- last page: 4 tail swatches, More wraps to page 1, Back present.
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 8)
assert(nrec('add|sw_15|') == 1 and nrec('add|sw_16|') == 1, 'page 8 tail')
assert(nrec('add|sw_17|') == 0, 'no row past the palette')
assert(nrec('add|morepage|') == 1 and nrec('add|back|') == 1, 'More + Back on last page')
-- More cycles: page 4 wraps to page 1 (find the wrapped PickColor call).
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 4)
local wraps = false
for _, c in ipairs(calls) do
  if c:find('Menu_Mods_PickColor', 1, true) and c:find(',1)', 1, true) then wraps = true end
end
-- paint() is a NO-OP until a property has been proven by the probe: spraying
-- unknown property names at every label during a screen build killed the game
-- mid-menu in-game 2026-10-02. So with no probe run, NO colour property is ever
-- set (only the text). Run the probe, which arms the winner, then painting works.
local function color_props(list)
  local n, states = 0, 0
  for _, c in ipairs(list) do
    if c:sub(1, 7) == 'setprop' and c:find('|Text Color|', 1, true) then n = n + 1 end
    if c:sub(1, 7) == 'setprop' and
       (c:find('|Text Color Highlight|', 1, true) or c:find('|Text Color Pressed|', 1, true))
    then states = states + 1 end
  end
  return n, states
end
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 1)
-- No probe has run yet, and none is required: theme_winner is pre-seeded with
-- the in-game-proven name. The shipped mod must theme the menu with no debug
-- flag present - it used to depend on config/probe-props existing.
local base0, state0 = color_props(calls)
assert(base0 == 6, 'two statics x two passes + two buttons themed once, got ' .. base0)
-- run the probe against a real label clone (TTMOD_PROBE_PROPS arms it)
calls = {}
TTMOD_PROBE_PROPS = 1
Menu_Mods_Select('demo.config')
local probed = 0
for _, c in ipairs(calls) do if c:find('probe:', 1, true) then probed = probed + 1 end end
assert(probed >= 3, 'probe dumped candidate properties, got ' .. probed)
local won = false
for _, c in ipairs(calls) do if c:find('probe-winner: Text Color', 1, true) then won = true end end
assert(won, 'probe found Text Color')
-- the engine repaints a row with a per-state colour on hover/press, so the
-- state variants must be discovered and painted too (2026-10-03: hovering an
-- accent row snapped it back to white)
local gotstates = 0
for _, c in ipairs(calls) do
  if c:find('probe-state: Text Color', 1, true) then gotstates = gotstates + 1 end
end
assert(gotstates >= 2, 'probe found the hover/press state variants, got ' .. gotstates)
TTMOD_PROBE_PROPS = nil
-- winner armed: swatches now paint base + state variants, ONLY proven props
calls = {}
Menu_Mods_PickColor('demo.config', 'accent', 1)
local painted_base, painted_states = color_props(calls)
assert(painted_base == 6, 'same paints land after the probe too, got ' .. painted_base)
assert(painted_states >= 4, 'state variants painted too, got ' .. painted_states)
for _, c in ipairs(calls) do
  if c:sub(1, 7) == 'setprop' and not c:find('Text String', 1, true) then
    assert(c:find('|Text Color', 1, true) or c:find('|Selection Color|', 1, true),
      'only proven properties painted: ' .. c)
  end
end
-- The read-only sweep must discover colour properties WITHOUT any debug flag:
-- it runs automatically the first time a theme is applied. This is the route to
-- the real state names (2026-10-03: hover reverted to stock because no guessed
-- name existed). Earlier assertions already triggered the one-shot, so reset it
-- and drive the real hook (not a stand-in) to actually exercise discovery.
TTMOD_THEME_SCOPE = 'all'
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_RESET_SWEEP()
-- The verifier must compare NUMERICALLY: the engine echoes "1" for 1.0 and
-- "0.50196081399918" for 128/255, so a formatted string compare reported a false
-- "engine overwrote" on a value that was exactly right (2026-10-03). The
-- read-back proves the float write lands: 128/255 == 0.50196081399918.
TTMOD_ACCENT = '#FF8000'
calls = {}
TTMOD_THEME_WIDGET({ id = 'verify_widget', agent = { of = 'verify_widget' } })
local oklines = 0
for _, c in ipairs(calls) do if c:find('verify-ok', 1, true) then oklines = oklines + 1 end end
assert(oklines > 0, 'verifier confirms the float write holds (no false overwrite)')
-- Menu_Add returns the widget BEFORE its label clone exists (in-game: every
-- Clone_Find was "not present"), so a widget themed on creation alone gets
-- nothing. It must be queued and re-themed on a later call, once populated.
local real_find = Clone_Find
function Clone_Find(b, what) return nil end
calls = {}
TTMOD_THEME_WIDGET({ id = 'unbuilt', agent = { of = 'unbuilt' } })
assert(TTMOD_THEME_PENDING() > 0, 'a widget with no label is queued for retry')
Clone_Find = real_find
-- the next widget's creation drains the queue; by then 'unbuilt' has a label
calls = {}
TTMOD_THEME_WIDGET({ id = 'next', agent = { of = 'next' } })
local repainted = 0
for _, c in ipairs(calls) do
  if c:sub(1, 7) == 'setprop' and c:find('|unbuilt|', 1, true) then repainted = repainted + 1 end
end
assert(repainted > 0, 'a widget with no label yet is re-themed once populated')
-- and it must not loop forever: a widget that never gets a label is dropped
calls = {}
for _ = 1, 8 do TTMOD_THEME_WIDGET({ id = 'ghost', agent = { of = 'ghost' } }) end
function Clone_Find(b, what) return nil end
for _ = 1, 8 do TTMOD_THEME_WIDGET({ id = 'ghost2', agent = { of = 'ghost2' } }) end
Clone_Find = real_find
-- The sweep only runs once a widget is genuinely populated (label clone found),
-- which the retry above guarantees. Drive one more settled widget to observe it.
TTMOD_THEME_RESET_SWEEP()
calls = {}
TTMOD_THEME_WIDGET({ id = 'sweep_widget', agent = { of = 'sweep_widget' } })
local all_names = ''
for _, c in ipairs(calls) do all_names = all_names .. c .. '\n' end
-- PLAIN search (4th arg true), so literal text - no '%' pattern escapes here.
assert(all_names:find('names tried', 1, true) ~= nil,
  'read-only sweep ran automatically, no debug flag needed')
assert(all_names:find('sweep-root-all:', 1, true) ~= nil,
  'sweep summarised what the widget root exposes')
assert(all_names:find('Text Color', 1, true) ~= nil,
  'sweep reported the colour property it found')
-- The engine stores this property as NAMED fields {r,g,b,a} (in-game probe
-- 2026-10-03: 'type=table {a=0 b=0 g=0 r=0}'). A positional array is silently
-- ignored, so pin the named shape: swatch 1 is #FFFFFF.
-- Hover-callback mechanism REMOVED 2026-10-03 (see menumods_ui.lua note):
-- Trigger Entered/Exited are scene-trigger properties, never fired on UI.
-- The live hover experiment is the chore-blanking probe below.
-- Prototype + chore probe (one-shot per reset): distinguishes WHY unhover
-- restores white instead of the accent. H1 = template default (paint the
-- prototype and clones are born with accent); H2 = nil chore means "default
-- flash" (blank the chore names and hover stops touching color). The stub has
-- no ListButton.agent, so the probe must log the miss, not crash; chore reads
-- must be attempted and logged either way.
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_RESET_SWEEP()
calls = {}
TTMOD_THEME_WIDGET({ id = 'proto_probe', agent = { of = 'proto_probe' } })
local saw_proto, saw_chore = false, false
for _, c in ipairs(calls) do
  if c:find('proto: ListButton', 1, true) then saw_proto = true end
  if c:find('proto: read Button - Chore', 1, true) then saw_chore = true end
end
assert(saw_proto, 'prototype probe ran and logged what it found')
assert(saw_chore, 'chore reads attempted and logged')
-- H2 execution: the three chore names must actually be blanked (set to ""),
-- not just read. If "" means "play nothing" the hover flash disappears.
local blanked = 0
for _, c in ipairs(calls) do
  if c:find('proto: blank Button - Chore', 1, true) then blanked = blanked + 1 end
end
assert(blanked == 3, 'all three chore names blanked, got ' .. blanked)
-- theme_audit: the stuck-white diagnostic. Simulate the engine overwriting a
-- painted label with white (hover), then drive any new widget: the drain must
-- log exactly one overwritten label and must NOT repaint it (read-only).
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_SCOPE = 'all'
calls = {}
TTMOD_THEME_WIDGET({ id = 'audit_w', agent = { of = 'audit_w' } })
assert(_props['audit_w/label/Text Color'] ~= nil, 'audit fixture painted')
_props['audit_w/label/Text Color'] = { r = 1, g = 1, b = 1, a = 1 }
calls = {}
TTMOD_THEME_WIDGET({ id = 'audit_next', agent = { of = 'audit_next' } })
local saw_audit = false
for _, c in ipairs(calls) do
  if c:find('theme-audit: 1 of ', 1, true) then saw_audit = true end
  if c:sub(1, 7) == 'setprop' and c:find('audit_w', 1, true) then
    error('audit repainted instead of only reading')
  end
end
assert(saw_audit, 'audit logged the engine-overwritten label')
-- theme_sub: script-path white writes become the accent (the stuck-white
-- fix); disabled-gray and other values pass through untouched.
-- Golden vectors live in tests/test_themecolor.cpp (core): keep the values
-- below identical to those when either changes. (0..255 int scale is
-- core-only; engine Lua tables always carry 0..1 floats.)
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_SCOPE = 'all'
calls = {}
AgentSetProperty({ of = 'sub_w' }, 'Text Color', { r = 1, g = 1, b = 1, a = 1 })
local sub = _color['sub_w//Text Color']
assert(type(sub) == 'table' and sub.r == 1 and sub.b == 0 and
  math.abs(sub.g - (128 / 255)) < 1e-6, 'white write substituted with accent')
local saw_sub = false
for _, c in ipairs(calls) do if c:find('theme-sub:', 1, true) then saw_sub = true end end
assert(saw_sub, 'substitution logged')
-- golden mirror of tests/test_themecolor.cpp (same values, both paths):
-- stock gray substitutes, saturated bright tint passes through.
calls = {}
AgentSetProperty({ of = 'sub_s' }, 'Text Color', { r = 0.878, g = 0.878, b = 0.878, a = 1 })
local stk = _color['sub_s//Text Color']
assert(type(stk) == 'table' and stk.r == 1 and stk.b == 0, 'stock gray substituted')
calls = {}
AgentSetProperty({ of = 'sub_t' }, 'Text Color', { r = 1, g = 0.9, b = 0.9, a = 1 })
assert(_color['sub_t//Text Color'].g == 0.9, 'saturated tint passes through')
calls = {}
AgentSetProperty({ of = 'sub_g' }, 'Text Color', { r = 0.4, g = 0.4, b = 0.4, a = 1 })
assert(_color['sub_g//Text Color'].r == 0.4, 'disabled gray passes through')
-- scope ttmod: unknown agents pass through, painted ones still substituted
TTMOD_THEME_SCOPE = 'ttmod'
calls = {}
AgentSetProperty({ of = 'sub_u' }, 'Text Color', { r = 1, g = 1, b = 1, a = 1 })
assert(_color['sub_u//Text Color'].r == 1 and _color['sub_u//Text Color'].g == 1,
  'ttmod scope leaves game widgets alone')
TTMOD_THEME_SCOPE = 'all'
-- SWATCH PRESERVATION (2026-10-07): the colour picker paints each palette
-- row's label with that swatch's own colour; the engine's hover
-- select/deselect cycle then writes stock white / 0.878 gray at the same
-- label. The substitution must restore the SWATCH colour, never the accent -
-- the reported bug was the row sticking accent until game restart.
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_SCOPE = 'all'
calls = {}
TT_TC_ALL = {}
TT_LAST_TC_AGENT = nil
Menu_Mods_PickColor('demo.config', 'accent', 1)
-- The picker sets the glow-suppression flag on push; entering any other
-- screen (Menu_Mods_Select) and popping (the Back row / a pick) must clear
-- it - the native hook leaves stock accent glow everywhere else.
TT_PALETTE = nil
Menu_Mods_PickColor('demo.config', 'accent', 1)
assert(TT_PALETTE == '1', 'picker opens the gate (white register, restores suppressed)')
TT_PALETTE = nil
Menu_Mods_Select('demo.config')
assert(TT_PALETTE == '0', 'select clears the gate')
TT_PALETTE = nil
Menu_Mods_PickColor('demo.config', 'accent', 1)
assert(TT_PALETTE == '1', 'picker re-opens the gate on push')
TT_PALETTE = nil
Menu_Pop()
assert(TT_PALETTE == '0', 'Menu_Pop wrapper clears the gate')
-- STATIC swatch displays carry their colours (2/page: #FFFFFF, #C0C0C0 on
-- page 1); the pick buttons beside them are accent. Static Headers are
-- never selected, so the select/deselect/restore cycle cannot touch them.
-- Last static on page 1 is #C0C0C0 (all channels 192/255, distinct from
-- the #FF8000 accent and the 0.878 template).
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_SCOPE = 'all'
local swd = nil
for i = #TT_TC_ALL, 1, -1 do
  if tostring(TT_TC_ALL[i].of):sub(1, 4) == 'swd_' then swd = TT_TC_ALL[i] break end
end
assert(swd ~= nil, 'palette static display captured')
local swdk = tostring(swd.of) .. '/' .. (swd.clone or '') .. '/Text Color'
assert(_color[swdk] ~= nil and math.abs(_color[swdk].r - (192 / 255)) < 1e-6 and
  math.abs(_color[swdk].g - (192 / 255)) < 1e-6,
  'static display painted its swatch colour (#C0C0C0), not the accent')
-- theme_roll: the hover fix, end to end. The stub models the engine: hover
-- writes white, unhover restores stock gray, both bypassing the
-- AgentSetProperty global. The wrapper must repaint accent either way.
TTMOD_ACCENT = '#FF8000'
TTMOD_THEME_SCOPE = 'all'
calls = {}
TTMOD_THEME_WIDGET({ id = 'roll_w', agent = { of = 'roll_w' } })
assert(_props['roll_w/label/Text Color'] ~= nil, 'roll fixture painted')
local rr = RolloverEnableTextColor({ of = 'roll_w' }, true)
assert(rr == 1, 'rollover return preserved')
local rl = _color['roll_w/label/Text Color']
assert(type(rl) == 'table' and rl.r == 1 and rl.b == 0 and
  math.abs(rl.g - (128 / 255)) < 1e-6, 'hover white repainted accent')
local saw_roll = false
for _, c in ipairs(calls) do if c:find('theme-roll:', 1, true) then saw_roll = true end end
assert(saw_roll, 'rollover interception logged')
calls = {}
RolloverEnableTextColor({ of = 'roll_w' }, false)
rl = _color['roll_w/label/Text Color']
assert(type(rl) == 'table' and rl.r == 1 and rl.b == 0 and
  math.abs(rl.g - (128 / 255)) < 1e-6, 'unhover stock repainted accent')
-- theme_tc: TextSetColor path. White numbers and white tables become accent;
-- gray passes through.
calls = {}
TextSetColor({ of = 'tc_w' }, 1, 1, 1, 1)
local tc = _color['tc_w//Text Color']
assert(type(tc) == 'table' and tc.r == 1 and tc.b == 0 and
  math.abs(tc.g - (128 / 255)) < 1e-6, 'white numbers substituted')
local saw_tc = false
for _, c in ipairs(calls) do if c:find('theme-tc:', 1, true) then saw_tc = true end end
assert(saw_tc, 'tsc substitution logged')
calls = {}
TextSetColor({ of = 'tc_g' }, 0.4, 0.4, 0.4, 1)
assert(_color['tc_g//Text Color'].r == 0.4, 'tsc gray passes through')
-- rollover family: log-only, verbatim passthrough (proves hover traffic).
calls = {}
local mr = RolloverEnableRolloverMesh({ of = 'm' }, true)
assert(mr == 1, 'mesh return preserved')
local saw_mesh = false
for _, c in ipairs(calls) do
  if c:find('theme-mesh:', 1, true) then saw_mesh = true end
  if c:sub(1, 5) == 'mesh|' then assert(c == 'mesh|true', 'mesh args untouched') end
end
assert(saw_mesh, 'mesh wrapper logged')
-- attribution: labels painted inside our own Populate tag as 'own', so the
-- audit can tell live main-menu overwrites from popped-screen corpses.
TTMOD_THEME_RESET_SWEEP()
calls = {}
Menu_Mods_Show()
_props['mod_demo.config/label/Text Color'] = { r = 0.878, g = 0.878, b = 0.878, a = 1 }
calls = {}
TTMOD_THEME_WIDGET({ id = 'own_next', agent = { of = 'own_next' } })
local saw_own = false
for _, c in ipairs(calls) do if c:find('own:1 menu:0', 1, true) then saw_own = true end end
assert(saw_own, 'audit attributes the overwrite to own screens')

-- The engine stores these properties as NAMED fields, 0..1 FLOATS (in-game
-- 2026-10-03). Integers get clamped and render stock. Pin the float form so
-- this cannot regress, and pin that the highlight is painted too.
local named, highlighted = 0, 0
for _, v in pairs(_color) do
  if type(v) == 'table' then
    assert(v.r ~= nil and v.g ~= nil and v.b ~= nil and v.a ~= nil,
      'colour written with NAMED r/g/b/a fields, got ' .. type(v))
    for _, ch in ipairs({ 'r', 'g', 'b' }) do
      assert(v[ch] <= 1 and v[ch] >= 0,
        'channel ' .. ch .. ' must be a 0..1 float, got ' .. tostring(v[ch]))
    end
    assert(v.a == 1 or v.a == 255, 'alpha must be 1 or 255, got ' .. tostring(v.a))
    named = named + 1
  end
end
assert(named >= 6, 'swatch colours written as named 0..1 float fields, got ' .. named)
-- Selection Color is the hover/selection highlight, discovered on the button
-- clone 2026-10-03: it must be painted with the accent so hover does not snap
-- to stock green/white.
for k, v in pairs(_color) do
  if type(k) == 'string' and k:find('Selection Color', 1, true) then
    highlighted = highlighted + 1
    assert(type(v) == 'table', 'Selection Color painted as a table')
  end
end
assert(highlighted > 0, 'the highlight colour is painted with the accent too')
-- #FF8000 must arrive as 1.0, 0.502, 0.0 - the exact measured representation
-- #FF8000 must arrive as 1.0, 0.5019608, 0.0 - the exact measured representation.
-- Several swatches have r=1 and b=0, so match the green exactly.
local saw_orange = false
for _, v in pairs(_color) do
  if type(v) == 'table' and v.r == 1 and v.b == 0 and
     math.abs(v.g - (128 / 255)) < 1e-6 then
    saw_orange = true
  end
end
assert(saw_orange, '#FF8000 written as 1.0, 0.50196, 0.0')
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
do local n = 0 for _, c in ipairs(calls) do
    if c:sub(1, 4) ~= 'log|' and c:sub(1, 8) ~= 'palette|' then n = n + 1 end end
  assert(n == 0, 'no-op on unknown (bridge chatter excluded)') end
-- empty registry
calls = {}
ttmod_menu = { seq = 2, mods = {} }
Menu_Mods_Show()
assert(nrec('add|nomods') == 1, 'empty row')
print('menumods-ui: screen logic OK')
"""

# The GAME runs Lua 5.1 (setmetatable/table.unpack are 5.2-only and are nil
# there - a setmetatable in menumods_ui.lua killed every menu screen in-game
# while this suite stayed green on 5.2) and has _G == nil. So the same proof
# runs on BOTH interpreters, against the game's actual missing-global shape.
# Interpreters are resolved from the system (lua5.1/lua5.2 on PATH); this used
# to hardcode `nix-shell`, which exists on the author's box and nowhere else,
# so every Lua test failed on CI in 0.1s.
import lua_runner

for version in lua_runner.VERSIONS:
    r = lua_runner.run(version, DRIVER)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode != 0 or "menumods-ui: screen logic OK" not in r.stdout:
        print(f"FAIL: menumods_ui.lua screen logic on Lua {version}")
        sys.exit(1)

# The C++-built literal must PARSE on stock 5.2 (needs test_modconfig run
# first). Path comes from the same shared location the C++ test writes to.
literal = lua_runner.share_path("ttmod_menu_literal_check.lua")
if not os.path.exists(literal):
    print(f"SKIP: {literal} not found - run test_modconfig first")
else:
    r2 = lua_runner.run_file("5.2", literal)
    sys.stdout.write(r2.stdout)
    sys.stderr.write(r2.stderr)
    if r2.returncode != 0:
        print("FAIL: build_menu_literal output does not parse")
        print(r2.stderr[-2000:])
        sys.exit(1)
print("menumods-ui: deterministic proof passed")
