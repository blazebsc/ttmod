-- Native Mods menu screens (TTMod). Runs INSIDE the game's menu Lua state:
-- defined once per captured state by the framework bridge, executed via
-- the native Button-Command/DoString path. Mirrors Menu_Options (disassembled
-- UI_ListButton.lua / Menu_Options.lua — VERIFIED against game bytecode):
--   menu = Menu_Create(ListMenu, 'ui_menu_options'); menu.align = 'left';
--   menu.background = {}; menu.Populate = function(self) <Menu_Add rows> end;
--   Menu_Push(menu)  (Populate runs on push — rows added pre-push land on the
--   CURRENT menu, i.e. they vanish into the main menu: the empty-screen bug)
-- Labels: Clone_Find wants the WIDGET AGENT (widget.agent, not the widget
-- table), child clone 'label', property 'Text String' — probed under pcall
-- (Clone_Find THROWS on a wrong target, which used to kill the screen).
-- Data: ttmod_menu table built by ttmod_menu_refresh() (C side).
-- Writes: ttmod_menu_set_enabled(id,"1"/"0"),
--         ttmod_menu_set_value(id,key,valuestring). All same-thread.

local function T(s)
    s = tostring(s)
    if EscapeText2 then s = EscapeText2(s) end
    return s
end

-- Trace into ttmod.log through the C bridge: click DoStrings swallow Lua
-- errors on this state, so every screen-build step logs its own progress.
local function mlog(s)
    if ttmod_menu_log ~= nil then ttmod_menu_log(s) end
end

-- Optional menu theming (menu-theme mod sets the bare global
-- TTMOD_ACCENT = "#RRGGBB" - NOT _G.TTMOD_ACCENT, because _G is nil in this
-- runtime). Absent = stock appearance (silent return, no log lines, zero
-- behavior change). Present = probe candidate color properties via pcall
-- (failures are silent engine-side); the first candidate the engine accepts
-- AND READS BACK wins for the session. Only our Mods-menu labels are themed
-- (via setlabel below); game screens are never touched.
-- Visible row count for the active list, or nil. Read-only probe used to size
-- screens: the engine renders a FIXED number of ListButton rows per menu, and
-- rows past that are added but never rendered (blank boxes, no label). Menus
-- stack rows, so this is the measured capacity of whatever is on screen now.
function Menu_Mods_RowCount()
    local n = 0
    local m = nil
    -- bare globals: _G is nil in this runtime
    if Menu_GetCurrent ~= nil then
        m = Menu_GetCurrent()
    elseif currentMenu ~= nil then
        m = currentMenu
    end
    if m == nil then return nil end
    for k, v in pairs(m) do
        if k ~= 'Populate' and type(v) == 'table' then
            local c = 0
            for _ in pairs(v) do c = c + 1 end
            if c > n then n = c end
        end
    end
    return n
end

-- Property-name probe (diagnostic). We do NOT know which AgentSetProperty key
-- sets a label's text colour - 'Color', 'Tint Color', ... were guesses and all
-- failed. This enumerates a real label clone: set each candidate, read it back
-- through AgentGetProperty, and log whatever actually sticks. One launch
-- answers the question; the names below are the union of every spelling seen
-- in Telltale UI scripts and engine widget property tables.
-- Enabled by the bare global TTMOD_PROBE_PROPS = 1 (the C++ side sets it when
-- TTMOD_PROBE=1 or config/probe-props exists).
-- Declaration order matters here: probe_props() references these, so they must
-- EXIST before it runs. A forward reference to a later `local` is a nil global
-- at call time, which killed the game mid-menu (2026-10-02, theme_int).
local TT_PROBE_DONE = false
-- Colour properties the framework paints on EVERY widget it sees. Discovered
-- in-game by read-only probing (AgentGetProperty on a real agent returns nil
-- for names that do not exist, which makes a safe sweep possible):
--   Text Color       - label/button text, proven
-- Buttons carry their own background/highlight properties; the sweep in
-- probe_props adds anything else it finds, so this list grows without a rebuild
-- once the engine's real names are known.
local theme_winner = 'Text Color'
local TT_COLOUR_PROPS = { 'Text Color', 'Label Color', 'Font Color' }
-- Per-role colours, settable from config by the theme mod. nil = leave stock.
local theme_roles = {}
-- State variants of the winner that the engine actually exposes, discovered by
-- probe_props. The engine repaints a row with a per-state colour on hover/
-- press, which is why an accent applied only to the base property reverts to
-- white the moment the cursor touches a row.
local theme_state_props = {}

-- DUMP EVERY REAL PROPERTY of an agent. The exe exports AgentGetProperties /
-- AgentGetClassProperties / AgentGetRuntimeProperties / AgentGetSceneProperties
-- / AgentGetTransientProperties / AgentHasProperty, but their LUA signatures are
-- not discoverable from the binary, so try the plausible shapes and report what
-- answered. Returns a space-separated name list, or nil.
local function dump_props(agent)
    if agent == nil then return nil end
    local fns = {
        { 'AgentGetProperties', AgentGetProperties },
        { 'AgentGetClassProperties', AgentGetClassProperties },
        { 'AgentGetRuntimeProperties', AgentGetRuntimeProperties },
        { 'AgentGetSceneProperties', AgentGetSceneProperties },
        { 'AgentGetTransientProperties', AgentGetTransientProperties },
    }
    local function names_of(list)
        if list == nil then return nil end
        local parts = {}
        if type(list) == 'string' then
            for w in string.gmatch(list, '[^%s,]+') do parts[#parts + 1] = w end
        elseif type(list) == 'table' then
            -- array of names, or of {name=..} / {..} records
            for _, v in ipairs(list) do
                if type(v) == 'string' then parts[#parts + 1] = v
                elseif type(v) == 'table' then
                    if type(v.name) == 'string' then parts[#parts + 1] = v.name
                    elseif type(v[1]) == 'string' then parts[#parts + 1] = v[1] end
                end
            end
            -- map shape: { ['Text Color'] = <value>, ... }
            for k, v in pairs(list) do
                if type(k) == 'string' and type(v) ~= 'table' then
                    local have = false
                    for _, p in ipairs(parts) do
                        if p == k then have = true break end
                    end
                    if not have then parts[#parts + 1] = k end
                end
            end
        else
            return nil
        end
        if #parts == 0 then return nil end
        return table.concat(parts, ' ')
    end
    -- Shapes to try, cheapest first: (agent), (agent, true), (agent, nil),
    -- (agent, '') for a "runtime only" / "class defaults" style flag.
    -- EVERY return value is inspected: a Lua C binding commonly returns
    -- (count, table), and reading only the first gave a number, which is why
    -- this reported "no shape returned names" on 2026-10-02 when the function
    -- had in fact answered.
    local shapes = {
        { name = '(agent)', call = function(f, a) return f(a) end },
        { name = '(agent,true)', call = function(f, a) return f(a, true) end },
        { name = '(agent,nil)', call = function(f, a) return f(a, nil) end },
        { name = '(agent,"")', call = function(f, a) return f(a, '') end },
    }
    for _, e in ipairs(fns) do
        local fname, fn = e[1], e[2]
        if fn ~= nil then
            for _, sh in ipairs(shapes) do
                local packed = { pcall(sh.call, fn, agent) }
                local ok = packed[1]
                if ok then
                    -- Log EVERY return's type first. This binding returns at
                    -- least a type name ('__ScriptObject') before the payload,
                    -- so accepting the first parseable string throws away the
                    -- answer. Seeing the full shape costs one launch; guessing
                    -- the signature costs a week.
                    if sh.name == '(agent)' then
                        local shapes_seen = {}
                        for i = 2, #packed do shapes_seen[#shapes_seen + 1] =
                            type(packed[i]) end
                        mlog('enumer-shape: ' .. fname .. ' returns ' .. #shapes_seen ..
                             ' value(s): ' .. table.concat(shapes_seen, ','))
                        for i = 2, #packed do
                            local t = type(packed[i])
                            if t == 'table' then
                                local n = 0
                                for _ in pairs(packed[i]) do n = n + 1 end
                                mlog('enumer-table: ' .. fname .. ' ret' .. (i - 1) ..
                                     ' has ' .. n .. ' entries')
                            elseif t == 'string' then
                                mlog('enumer-str: ' .. fname .. ' ret' .. (i - 1) ..
                                     ' = ' .. string.sub(packed[i], 1, 120))
                            end
                        end
                    end
                    for i = #packed, 2, -1 do
                        local got = names_of(packed[i])
                        if got ~= nil then
                            return fname .. sh.name .. ' ret' .. (i - 1) .. ' ' .. got
                        end
                    end
                end
            end
            mlog('probe-all: ' .. fname .. ' present, no shape returned names')
        else
            mlog('probe-all: ' .. fname .. ' missing')
        end
    end
    return nil
end
local TT_PROP_CANDIDATES = {
    'Color', 'Tint Color', 'Font Color', 'Text Color', 'Diffuse', 'Colour',
    'Color Tint', 'Tint', 'TextColour', 'TextColor', 'FontColour', 'FontColor',
    'Label Color', 'Label Text Color', 'Foreground Color', 'Label Tint',
    'ColorModulate', 'Modulate Color', 'Emissive Color', 'Text Diffuse',
    'Color State Normal', 'ColorNormal', 'Text Color Normal', 'Font Color Normal',
    'Color Highlight', 'Color Pressed', 'Color Disabled', 'Color Disabled Text',
    -- State variants of the PROVEN name. Hovering a row resets it to white, so
    -- the hover/pressed/selected variants need the accent too (verified in-game
    -- 2026-10-03: 'Text Color' holds but hover overwrites it).
    'Text Color Highlight', 'Text Color Hover', 'Text Color Pressed',
    'Text Color Selected', 'Text Color Focus', 'Text Color Active',
    'Text Color State Normal', 'Text Color Normal', 'Text Color Disabled',
    'Text Color Highlighted', 'Text ColorPressed', 'TextColorHighlight',
}
local function probe_props(agent)
    -- Log FIRST and never tostring() the agent: engine agents are userdata whose
    -- __tostring can fault, and a fault here kills the game mid-menu. Identity is
    -- never needed - only which property names read back.
    mlog('probe: begin')
    if agent == nil then
        mlog('probe: no agent')
        return
    end
    mlog('probe: getprop=' .. type(AgentGetProperty) .. ' setprop=' .. type(AgentSetProperty))
    -- Truth first: the agent's real property list. Guessing names is what cost
    -- a day; the exe exports AgentGetProperties/AgentGetClassProperties/
    -- AgentGetRuntimeProperties, so just ask.
    do
        local list = dump_props(agent)
        if list ~= nil then
            mlog('probe-all: ' .. list)
        else
            mlog('probe-all: unavailable')
        end
    end
    for _, prop in ipairs(TT_PROP_CANDIDATES) do
        local ok = pcall(AgentSetProperty, agent, prop, '#FF00FF')
        local back, is_ours = nil, false
        if ok and AgentGetProperty ~= nil then
            local ok2, v = pcall(AgentGetProperty, agent, prop)
            if ok2 then
                back = v
                -- Compare by TYPE-safe identity: only strings and numbers can
                -- be compared without risking a metamethod fault.
                if type(v) == 'string' then is_ours = (v == '#FF00FF')
                elseif type(v) == 'number' then is_ours = (v == 4294902015) end
            end
        end
        local readable = back ~= nil
        -- readable-but-not-ours => property EXISTS, wrong value form. type() and
        -- a length-bounded string are safe on any value. For a table, dump its
        -- KEYS and scalar field values: that is the value shape we must write
        -- back (a colour property returning a table almost always wants
        -- {r,g,b[,a]}), and it is the only way to learn the format in one pass.
        local detail = ''
        if readable then
            local tv = type(back)
            detail = ' type=' .. tv
            if tv == 'string' then
                detail = detail .. ' val=' .. string.sub(back, 1, 32)
            elseif tv == 'number' then
                detail = detail .. ' val=' .. tostring(back)
            elseif tv == 'table' then
                local parts = {}
                for k, v in pairs(back) do
                    local kt = type(v)
                    if kt == 'number' or kt == 'string' or kt == 'boolean' then
                        parts[#parts + 1] = tostring(k) .. '=' .. tostring(v)
                    else
                        parts[#parts + 1] = tostring(k) .. '=<' .. kt .. '>'
                    end
                end
                table.sort(parts)
                detail = detail .. ' {' .. table.concat(parts, ' ') .. '}'
            end
        end
        mlog('probe: ' .. prop .. ' set=' .. tostring(ok) .. ' reads=' .. tostring(readable) ..
             (is_ours and ' *** MATCH ***' or '') .. detail)
        -- Any property that reads back EXISTS. theme_winner arrives pre-seeded
        -- with the proven name, so this confirms it and collects any OTHER
        -- readable property as a per-state variant - those are what the engine
        -- repaints with on hover/press (accent on the base property alone
        -- reverts to stock the moment the cursor touches a row). Never
        -- early-return: variants only turn up after the winner in the list.
        if readable then
            if prop == theme_winner then
                mlog('probe-winner: ' .. prop ..
                     (is_ours and ' (confirmed)' or ' (confirmed, value format differs)'))
            else
                local already = false
                for _, p in ipairs(theme_state_props) do
                    if p == prop then already = true break end
                end
                if not already then
                    theme_state_props[#theme_state_props + 1] = prop
                    mlog('probe-state: ' .. prop)
                end
            end
        end
    end
    -- Nothing matched by value. Report which properties read back at all: the
    -- real name is in that set even if the value format stays unknown.
    local readable = {}
    if AgentGetProperty ~= nil then
        for _, prop in ipairs(TT_PROP_CANDIDATES) do
            local ok, v = pcall(AgentGetProperty, agent, prop)
            if ok and v ~= nil then readable[#readable + 1] = prop end
        end
    end
    mlog('probe: readable = ' .. table.concat(readable, ', '))
    if theme_winner ~= nil and #theme_state_props > 0 then
        mlog('probe-winner: ' .. theme_winner .. ' + states: ' ..
             table.concat(theme_state_props, ' '))
    elseif theme_winner ~= nil then
        mlog('probe-winner: ' .. theme_winner .. ' (no state variants)')
    end
    mlog('probe: end')
end

local theme_probed = false
-- Swatch preview cache: per-agent last paint, so re-rendering a row does not
-- re-probe or fight the engine's own refresh. Plain table, NOT setmetatable:
-- the game's Lua is 5.1 (no setmetatable, no table.unpack) - verified in-game.
local theme_painted = {}
local function theme_int(s)
    if type(s) ~= 'string' then return nil end
    local r, g, b = s:match('^#(%x%x)(%x%x)(%x%x)$')
    if r == nil then return nil end
    return 255 * 16777216 + tonumber(r, 16) * 65536 + tonumber(g, 16) * 256 + tonumber(b, 16)
end
-- Write one colour to one property, trying the value shapes the engine may
-- want. Returns true on the first shape it accepts.
-- Write one colour to one property.
--
-- MEASURED IN-GAME 2026-10-03: the engine stores this property as FLOATS in
-- 0..1, not 0..255. Read-back of our first successful write was
--   0.87843102216721,0.87843102216721,0.87843102216721,1
-- i.e. our integer 255/128/0/255 was clamped into range, which is why the row
-- rendered stock and why every earlier "it works" read-back looked like the
-- engine had overwritten us - it had, with the clamped value.
-- So the normalised 0..1 form is tried FIRST, and it is the correct one.
local function set_color(agent, prop, hex)
    local r, g, b = hex:match('^#(%x%x)(%x%x)(%x%x)$')
    if r == nil then return false end
    local num = theme_int(hex)
    local ri, gi, bi = tonumber(r, 16), tonumber(g, 16), tonumber(b, 16)
    -- 0..1 floats: the engine's actual representation
    local fr, fg, fb = ri / 255, gi / 255, bi / 255
    if pcall(AgentSetProperty, agent, prop, { r = fr, g = fg, b = fb, a = 1 }) then return true end
    -- Fallbacks for other builds/properties that may still want other shapes.
    if pcall(AgentSetProperty, agent, prop, { r = ri, g = gi, b = bi, a = 255 }) then return true end
    if pcall(AgentSetProperty, agent, prop, hex) then return true end
    if pcall(AgentSetProperty, agent, prop, { ri, gi, bi, 255 }) then return true end
    if pcall(AgentSetProperty, agent, prop, { ri, gi, bi }) then return true end
    if num ~= nil and pcall(AgentSetProperty, agent, prop, num) then return true end
    return false
end
-- Paint `agent` with a specific hex, but ONLY with properties already proven by
-- probe_props. Until one exists this is a deliberate no-op: spraying unknown
-- property names at every label during a screen build is what killed the game
-- mid-menu on 2026-10-02. Discovery happens once, inside probe_props.
-- Selection/highlight colours, discovered in-game 2026-10-03 by sweeping the
-- engine's DASHED property namespace on the button clone:
--   Selection Color = {r=0.5, g=1, b=0.5, a=1}  (light green, stock)
-- This is the row's hover/selection highlight. Painted with the accent so a
-- highlighted row reads as accent-on-accent instead of snapping to stock.
-- ponytail: single discovered name; if press/disabled variants are ever found,
-- add them here rather than adding a discovery mechanism.
local TT_STATE_COLOUR_PROPS = { 'Selection Color' }
local function paint(agent, hex)
    if agent == nil or pcall == nil or AgentSetProperty == nil then return false end
    if theme_winner == nil then return false end
    if not set_color(agent, theme_winner, hex) then return false end
    -- State variants too, so hover/press does not snap back to stock colour.
    for _, p in ipairs(theme_state_props) do
        if p ~= theme_winner then set_color(agent, p, hex) end
    end
    -- Highlight colours live on the BUTTON clone, not the label; writing them
    -- where they do not exist is a pcall'd no-op, so paint everywhere we touch.
    for _, p in ipairs(TT_STATE_COLOUR_PROPS) do
        set_color(agent, p, hex)
    end
    return true
end
local function apply_theme(agent)
    -- Bare global, NOT _G.TTMOD_ACCENT: _G is NIL in the game's Lua runtime
    -- (verified in-game 2026-10-02), so the _G form silently disabled theming.
    local acc = TTMOD_ACCENT
    if type(acc) ~= 'string' then return end
    if agent == nil then return end
    if theme_painted[agent] == acc then return end
    if paint(agent, acc) then theme_painted[agent] = acc
    elseif not theme_probed then
        mlog('theme-winner: none')
        theme_probed = true
    end
end

-- Theme a whole widget, not just its label: the button's own agent carries the
-- background/highlight properties. Called for EVERY widget the engine creates
-- (our screens and the game's own), so one colour in config re-themes all menus.
-- Best-effort and fully pcall'd: this runs inside the engine's own screen build.
--
-- READ-ONLY property discovery. AgentGetProperty on a real agent returns nil
-- for a name that does not exist, so sweeping hundreds of candidates is safe -
-- no writes, no flashes, nothing for the engine to fault on. The earlier probe
-- WROTE values, which caused the magenta flash and, with a forward reference,
-- the process kill. Runs ONCE per session, automatically, the first time a
-- theme is applied: discovery is triggered by need, not by a debug file.
local TT_SWEEP_DONE = false
-- One-shot for the prototype+chore probe below. Declared up here with the
-- other one-shots: a later `local` would be a nil global at call time, which
-- kills the process inside a click callback instead of raising an error.
local TT_PROTO_DONE = false
local TT_SWEEP_BASES = {
    'Text', 'Label', 'Font', 'Button', 'Bg', 'Background', 'Panel', 'Row',
    'Widget', 'Highlight', 'Hover', 'Pressed', 'Press', 'Select', 'Selected',
    'Focus', 'Active', 'Icon', 'Title', 'Header', 'Caption', 'Backdrop',
    'Overlay', 'Fill', 'Glow', 'Shadow', 'Edge', 'Border', 'Accent', 'Tint',
    'Diffuse', 'Modulate', 'State',
}
local TT_SWEEP_SUFFIXES = {
    'Color', 'Colour', 'ColorNormal', 'ColorHighlight', 'ColorHover',
    'ColorPressed', 'ColorSelected', 'ColorDisabled', 'StateNormal',
}
-- The engine's own property namespace uses DASHED names ('Button - Command',
-- 'Text String' - both verified in our research). The colour-name sweep above
-- never covered that family, which is where a highlight/state property would
-- most plausibly live. Read-only, so just try them.
local TT_SWEEP_DASHED = {
    'Button - Command', 'Button - Highlight', 'Button - State',
    'Button - Selected', 'Button - Hover', 'Button - Pressed',
    'Button - Normal', 'Button - Disabled', 'Button - Focus',
    'Highlight', 'Highlighted', 'Hover', 'Hovered', 'State',
    'Selected', 'Pressed', 'Normal', 'Disabled', 'Focus', 'Focused',
    'Enabled', 'Visible', 'Active', 'Tint Color', 'Image Color',
    'Highlight Color', 'Selection Color',
}
local function sweep_props(agent, tag)
    if agent == nil or AgentGetProperty == nil then return end
    local found = {}
    local listed = dump_props(agent)
    if listed ~= nil then
        mlog('sweep-' .. tag .. '-enumerated: ' .. listed)
        for w in string.gmatch(listed, '[^%s,]+') do found[w] = true end
    end
    local tries = 0
    for _, base in ipairs(TT_SWEEP_BASES) do
        for _, suf in ipairs(TT_SWEEP_SUFFIXES) do
            local name = base .. ' ' .. suf
            tries = tries + 1
            local ok, v = pcall(AgentGetProperty, agent, name)
            if ok and v ~= nil then
                found[name] = true
                mlog('sweep-' .. tag .. '-found: ' .. name .. ' type=' .. type(v))
            end
        end
    end
    -- The engine's own dashed property namespace ('Button - Command',
    -- 'Text String') plus bare state names - a highlight/state property would
    -- live here, and this family was never tried before (2026-10-03 gap).
    for _, name in ipairs(TT_SWEEP_DASHED) do
        tries = tries + 1
        local ok, v = pcall(AgentGetProperty, agent, name)
        if ok and v ~= nil then
            found[name] = true
            -- Log the VALUE for state names: knowing Highlight reads false (or
            -- 0, or a table) is itself the discovery.
            local tv = type(v)
            local detail = ''
            if tv == 'string' then detail = ' val=' .. string.sub(v, 1, 24)
            elseif tv == 'number' or tv == 'boolean' then detail = ' val=' .. tostring(v)
            elseif tv == 'table' then
                local parts = {}
                for k, fv in pairs(v) do
                    if type(fv) == 'number' or type(fv) == 'string' or
                       type(fv) == 'boolean' then
                        parts[#parts + 1] = tostring(k) .. '=' .. tostring(fv)
                    else
                        parts[#parts + 1] = tostring(k) .. '=<' .. type(fv) .. '>'
                    end
                end
                table.sort(parts)
                detail = ' {' .. table.concat(parts, ' ') .. '}'
            end
            mlog('sweep-' .. tag .. '-found: ' .. name .. ' type=' .. tv .. detail)
        end
    end
    mlog('sweep-' .. tag .. ': ' .. tries .. ' names tried')
    local names = {}
    for k in pairs(found) do names[#names + 1] = k end
    table.sort(names)
    if #names > 0 then
        mlog('sweep-' .. tag .. '-all: ' .. table.concat(names, ' | '))
    else
        mlog('sweep-' .. tag .. '-all: (none readable)')
    end
end
-- Menu_Add returns the widget BEFORE its label child exists, so a single
-- immediate pass finds no clone and themes nothing (verified in-game
-- 2026-10-03: every Clone_Find returned "not present"). The engine populates
-- each row during its own Populate pass, so the NEXT widget's creation is a
-- reliable later moment to retry. Queue unthemed widgets and drain on the next
-- theme_widget call - no per-frame engine hook exists, and inventing one would
-- be a new hook surface for a cosmetic gain. Bounded: a widget is retried at
-- most a few times, then dropped.
local TT_PENDING = {}
local TT_PENDING_MAX = 32
local function theme_pending(widget)
    if #TT_PENDING >= TT_PENDING_MAX then return end
    TT_PENDING[#TT_PENDING + 1] = { widget, 0 }
end
-- Forward declaration. theme_drain calls theme_widget_settled, which is defined
-- below; referencing a later `local` is a nil global at call time (this bit us
-- before with theme_int and it KILLED the game inside a click callback). Declare
-- it here and assign later, so the ordering is explicit.
local theme_widget_settled
local function theme_drain()
    if #TT_PENDING == 0 then return end
    local queue = TT_PENDING
    TT_PENDING = {}
    for _, entry in ipairs(queue) do
        local widget, tries = entry[1], entry[2] + 1
        -- theme_widget_settled re-queues itself if the label is still missing
        if tries <= 4 then
            local ok, settled = pcall(theme_widget_settled, widget)
            if not ok or settled ~= true then
                theme_pending(widget)
            end
        end
    end
end
-- The actual theming pass. Returns true once the widget is settled (a label
-- clone was found and themed), false while it is still unpopulated.
-- Numeric comparison instead of string compare. The engine echoes back whatever
-- precision it likes ("1" for 1.0, "0.50196081399918" for 128/255), so a
-- formatted string comparison reported a false "engine overwrote" on a value
-- that was in fact exactly right - which sent us hunting a bug that did not
-- exist. Compare components within a tolerance.
local function colour_matches(v, ir, ig, ib)
    if type(v) ~= 'table' then return nil end
    local function near(a, b)
        if type(a) ~= 'number' then return false end
        -- accept either 0..1 or 0..255
        if math.abs(a - b / 255) < 1e-4 then return true end
        return math.abs(a - b) < 1e-4
    end
    return near(v.r, ir) and near(v.g, ig) and near(v.b, ib)
end

-- Verify the write: read the value back after painting. Proved its point
-- (2026-10-03): the engine stores 0..1 FLOATS, so the integer form was clamped
-- and rendered stock. Now that the format is known and pinned by a test, this
-- runs ONCE per session purely as a health check - one verify-ok line is the
-- all-clear; per-widget logging was 30% of the whole runtime log.
local TT_VERIFY_DONE = false
local function theme_verify(agent, tag)
    if agent == nil or AgentGetProperty == nil or theme_winner == nil then return end
    local ok, v = pcall(AgentGetProperty, agent, theme_winner)
    if not ok or v == nil then return end
    local acc0 = TTMOD_ACCENT
    if type(acc0) == 'string' then
        local r0, g0, b0 = acc0:match('^#(%x%x)(%x%x)(%x%x)$')
        if r0 ~= nil then
            local want = colour_matches(v, tonumber(r0, 16), tonumber(g0, 16),
                                       tonumber(b0, 16))
            if want == true then
                mlog('verify-ok: ' .. theme_winner .. ' holds (exact)' ..
                     (tag and (' on ' .. tag) or ''))
            else
                mlog('verify: ' .. theme_winner .. ' does not hold' ..
                     (tag and (' on ' .. tag) or ''))
            end
            return
        end
    end
    local cur
    local t = type(v)
    if t == 'table' then
        cur = string.format('%s,%s,%s,%s', tostring(v.r), tostring(v.g),
                            tostring(v.b), tostring(v.a))
    elseif t == 'string' then cur = v
    elseif t == 'number' then cur = tostring(v)
    else return end
    local acc = TTMOD_ACCENT
    if type(acc) ~= 'string' then return end
    local r, g, b = acc:match('^#(%x%x)(%x%x)(%x%x)$')
    if r == nil then return end
    -- Compare in BOTH units: the engine stores 0..1 floats, and a mismatch in
    -- the other direction is just as informative as the one we hit.
    local ir, ig, ib = tonumber(r, 16), tonumber(g, 16), tonumber(b, 16)
    local want255 = string.format('%d,%d,%d,%d', ir, ig, ib, 255)
    local want01 = string.format('%.6f,%.6f,%.6f,%.6f', ir / 255, ig / 255,
                                 ib / 255, 1)
    if cur ~= want255 and cur ~= want01 then
        mlog('verify: ' .. theme_winner .. ' = ' .. cur .. ' (wanted ' .. want255 ..
             ' or ' .. want01 .. ') -> engine overwrote' ..
             (tag and (' on ' .. tag) or ''))
    else
        mlog('verify-ok: ' .. theme_winner .. ' holds ' .. cur ..
             (tag and (' on ' .. tag) or ''))
    end
end

-- NOTE (2026-10-03): the Trigger Entered/Exited Callback mechanism that used
-- to live here is gone. Those are 3D scene-trigger properties; across every
-- in-game session the callbacks fired zero times on UI widgets. Removed rather
-- than left to mislead. See the chore-blanking probe in theme_widget_settled.

theme_widget_settled = function(widget)
    -- true  = settled (or deliberately skipped), stop retrying
    -- false = the widget is still being built, retry on a later call
    if widget == nil or pcall == nil then return true end
    -- Scope: "all" (default) re-themes the game's own menus as well as ours,
    -- because the Menu_Add wrapper routes every widget here. "ttmod" leaves the
    -- game's own screens alone. Set by the menu-theme mod from config.
    if TTMOD_THEME_SCOPE == 'ttmod' then return true end
    if type(TTMOD_ACCENT) ~= 'string' and next(theme_roles) == nil then return true end
    local ok, ag = pcall(function()
        return (widget.agent ~= nil) and widget.agent or widget
    end)
    if not ok or ag == nil then return true end
    pcall(apply_theme, ag)
    -- its common children, which is where the visible text lives
    local found_any = false
    if Clone_Find ~= nil then
        for _, child in ipairs({ 'label', 'caption', 'text', 'ui_listButton_label',
                                 'ui_header_header' }) do
            pcall(function()
                local okc, c = pcall(Clone_Find, ag, child)
                if okc and c ~= nil then
                    apply_theme(c)
                    found_any = true
                end
            end)
        end
    end
    -- One read-only discovery pass per session, once a widget really is
    -- populated. Walks the REAL clone chain from UI_ListButton.lua (our own
    -- research, docs/runtime/in-game-mod-menu.md):
    --   widget.agent -> 'ui_listButton_button' -> .agent -> 'label'
    -- The button-box clone was never swept before (2026-10-03 gap) and it is
    -- where a hover-highlight property would live: sweeps are read-only and
    -- safe, so cover every hop.
    if not TT_SWEEP_DONE and found_any then
        TT_SWEEP_DONE = true
        pcall(sweep_props, ag, 'root')
        if Clone_Find ~= nil then
            for _, child in ipairs({ 'label', 'ui_listButton_label', 'caption', 'text',
                                     'ui_header_header', 'ui_listButton_button' }) do
                pcall(function()
                    local okc, c = pcall(Clone_Find, ag, child)
                    if not okc then
                        mlog('sweep-child ' .. child .. ': Clone_Find threw')
                    elseif c == nil then
                        mlog('sweep-child ' .. child .. ': not present')
                    else
                        sweep_props(c, child)
                        -- the real chain continues THROUGH the button clone
                        if child == 'ui_listButton_button' and c ~= nil then
                            local oka, ba = pcall(function()
                                return (c.agent ~= nil) and c.agent or c
                            end)
                            if oka and ba ~= nil then
                                sweep_props(ba, 'button-agent')
                                local okl, lab = pcall(Clone_Find, ba, 'label')
                                if okl and lab ~= nil then
                                    sweep_props(lab, 'button-label')
                                end
                            end
                        end
                    end
                end)
            end
        end
    end
    -- PROTOTYPE + CHORE probe (one-shot per session). Two hypotheses for why
    -- unhover restores white instead of our accent, both testable without RE:
    --
    -- H1 (template default): the engine snapshots the label color at widget
    -- creation and restores THAT on unhover. Our paint lands after creation,
    -- so the snapshot is the stock white. If the ListButton PROTOTYPE has a
    -- settable label agent, painting it means clones are born with accent and
    -- the snapshot is accent too. Permanent fix if true, highlight preserved.
    --
    -- H2 (nil chore = default flash): rows have no 'Button - Chore Select'
    -- set, so the engine plays its hardcoded white flash. If "" disables it
    -- (vs nil meaning "use default"), hover stops touching color at all.
    -- Highlight feedback is lost, but accent never leaves. Fallback only.
    if not TT_PROTO_DONE and found_any then
        TT_PROTO_DONE = true
        pcall(function()
            mlog('proto: ListButton type=' .. type(ListButton))
            local pa = (type(ListButton) == 'table') and ListButton.agent or nil
            mlog('proto: ListButton.agent type=' .. type(pa))
            if pa ~= nil and AgentSetProperty ~= nil then
                local okc, c = pcall(Clone_Find, pa, 'label')
                mlog('proto: Clone_Find label ok=' .. tostring(okc))
                if okc and c ~= nil and TTMOD_ACCENT ~= nil then
                    local okw = pcall(AgentSetProperty, c, 'Text Color',
                                      TTMOD_ACCENT)
                    mlog('proto: paint Text Color ok=' .. tostring(okw))
                end
            end
            -- H2: read current chore values, then blank them on THIS widget.
            -- nil chore = engine plays its default white flash. If "" means
            -- "play nothing" (vs nil meaning "use default"), hover stops
            -- touching color entirely: accent stays always, at the cost of
            -- losing the highlight flash. Fully pcall'd; worst case the
            -- engine treats "" like nil and nothing changes.
            if AgentGetProperty ~= nil and ag ~= nil then
                for _, k in ipairs({'Button - Chore Select',
                                    'Button - Chore Deselect',
                                    'Button - Chore Press'}) do
                    local okr, v = pcall(AgentGetProperty, ag, k)
                    mlog('proto: read ' .. k .. ' ok=' .. tostring(okr) ..
                         ' val=' .. tostring(v))
                    if AgentSetProperty ~= nil then
                        local okw = pcall(AgentSetProperty, ag, k, '')
                        mlog('proto: blank ' .. k .. ' ok=' .. tostring(okw))
                    end
                end
            end
        end)
    end
    -- No label clone yet: the widget is still being built. Queue it and let
    -- the next theme_widget call retry once the engine has populated it.
    if not found_any then
        theme_pending(widget)
        return false
    end
    -- Prove the write landed, and whether something later overwrites it.
    for _, child in ipairs({ 'label', 'ui_listButton_label', 'caption', 'text',
                             'ui_header_header' }) do
        pcall(function()
            local okc, c = pcall(Clone_Find, ag, child)
            if okc and c ~= nil and not TT_VERIFY_DONE then
                TT_VERIFY_DONE = true
                theme_verify(c, child)
            end
        end)
    end
    -- NOTE (2026-10-03): the Trigger Entered/Exited Callback registration that
    -- used to live here is gone. It set scene-trigger properties on UI widgets;
    -- across every in-game session the callbacks fired zero times (the engine
    -- only honors them on 3D scene trigger volumes, not UI). Dead code removed
    -- rather than left to mislead; the chore-blanking above is the live hover
    -- experiment.
    return true
end
-- Exposed so the Menu_Add wrapper (loader/windows/menu_bridge.hpp) can theme
-- every widget the engine creates, including the game's own screens.
-- Drains the retry queue FIRST: by the time the next widget is added the
-- previous one has been populated and can finally be themed.
function TTMOD_THEME_WIDGET(widget)
    theme_drain()
    pcall(theme_widget_settled, widget)
end
-- Re-apply the theme to a widget after an interaction (click/selection), for
-- engines that repaint rows from their own state on interaction. Clears the
-- per-agent cache first so the next pass really writes again.
TTMOD_THEME_REFRESH = function(widget)
    if widget == nil then return end
    theme_painted = {}
    pcall(theme_widget_settled, widget)
end
-- Test seam: force the next themed widget to sweep again AND drop the per-agent
-- paint cache (an agent already painted would otherwise return early and the
-- sweep would be the only thing left to observe). Only used by
-- tests/test_menumods_ui.py, which cannot reach these upvalues directly.
TTMOD_THEME_RESET_SWEEP = function()
    TT_SWEEP_DONE = false
    TT_PROTO_DONE = false
    theme_painted = {}
    TT_PENDING = {}
    TT_VERIFY_DONE = false
end
-- Test seam: how many widgets are waiting for a retry.
TTMOD_THEME_PENDING = function() return #TT_PENDING end

-- Returns the label agent it wrote to (nil when nothing usable was found), so
-- callers that need to paint the same clone don't have to re-find it.
local function setlabel(btn, text)
    if btn == nil then mlog('setlabel: nil btn') return nil end
    if pcall == nil then mlog('setlabel: no pcall') return nil end
    local ag = btn.agent ~= nil and btn.agent or btn
    for i, n in ipairs({'label', 'ui_header_header', 'ui_listButton_label', 'caption', 'text'}) do
        local ok, lab = pcall(Clone_Find, ag, n)
        if ok and lab ~= nil then
            local ok2 = pcall(AgentSetProperty, lab, 'Text String', T(text))
            mlog('setlabel: ' .. n .. ' propset=' .. tostring(ok2))
            if ok2 then
                -- Diagnostic: probe once, on the first real label clone.
                if TTMOD_PROBE_PROPS == 1 and not TT_PROBE_DONE then
                    TT_PROBE_DONE = true
                    probe_props(lab)
                    -- The engine repaints the ROW (the button agent), not the
                    -- label, on hover - so enumerate that too. Hover-state
                    -- colours live there.
                    if ag ~= nil then
                        local blist = dump_props(ag)
                        mlog('probe-all-button: ' .. (blist or 'unavailable'))
                    end
                end
                apply_theme(lab)
                return lab
            end
        end
    end
    mlog('setlabel: no usable label clone')
    return nil
end

local function cbquote(s)
    return (tostring(s):gsub('\\', '\\\\'):gsub('"', '\\"'))
end

function Menu_Mods_Noop()
end

-- Native string editing via the engine's own modal text box (the save-
-- rename idiom): synchronous (text, ok); input disabled around it; cancel
-- changes nothing. No custom keyboard, no new widgets.
function Menu_Mods_EditString(id, key)
    if Menu_OpenTextEntryBox == nil then return end
    local m = Menu_Mods_Find(id)
    if m == nil or m.config == nil then return end
    for _, c in ipairs(m.config) do
        local ct = tostring(c.type)
        if tostring(c.key) == tostring(key) and (ct == 'string' or ct == 'color') then
            if WidgetInputHandler_EnableInput ~= nil then
                WidgetInputHandler_EnableInput(false)
            end
            local s, ok = Menu_OpenTextEntryBox(tostring(c.value), tostring(c.label))
            if WidgetInputHandler_EnableInput ~= nil then
                WidgetInputHandler_EnableInput(true)
            end
            if ok then
                ttmod_menu_set_value(tostring(id), tostring(key), tostring(s))
            end
            break
        end
    end
    Menu_Pop()
    Menu_Mods_Select(id)
end

function Menu_Mods()
    mlog('mods: enter')
    ttmod_menu_refresh()
    Menu_Mods_Show()
end

-- Real menu idiom (Menu_Options.lua): create, set align/background, assign
-- Populate, push. Rows MUST be added inside Populate (runs on push).
function Menu_Mods_Show()
    mlog('show: enter listmenu=' .. type(ListMenu) .. ' header=' .. type(Header) ..
        ' listbutton=' .. type(ListButton) .. ' create=' .. type(Menu_Create) ..
        ' push=' .. type(Menu_Push))
    if Menu_Create == nil or Menu_Add == nil or Menu_Push == nil then
        mlog('show: engine menu globals missing, aborting')
        return
    end
    local data = ttmod_menu
    local menu = Menu_Create(ListMenu, 'ui_menu_options')
    if menu == nil then mlog('show: create failed') return end
    menu.align = 'left'
    menu.background = {}
    menu.Populate = function(self)
        mlog('populate: enter')
        local h = Menu_Add(Header, nil, 'header_settings')
        setlabel(h, 'Mods')
        if data == nil or data.mods == nil or #data.mods == 0 then
            local r = Menu_Add(ListButton, 'nomods', 'label_OK', 'Menu_Mods_Noop()')
            setlabel(r, 'No mods installed')
        else
            for i, m in ipairs(data.mods) do
                local st = m.enabled and 'ON' or 'OFF'
                local r = Menu_Add(ListButton, 'mod_' .. tostring(m.id), 'label_OK',
                    'Menu_Mods_Select("' .. cbquote(m.id) .. '")')
                setlabel(r, tostring(m.name) .. '  ' .. tostring(m.version) .. '  [' .. st .. ']')
            end
        end
        local b = Menu_Add(ListButton, 'back', 'label_OK', 'Menu_Pop()')
        setlabel(b, 'Back')
        mlog('populate: done')
    end
    Menu_Push(menu)
    mlog('show: pushed')
end

function Menu_Mods_Find(id)
    local data = ttmod_menu
    if data == nil or data.mods == nil then return nil end
    for _, m in ipairs(data.mods) do
        if tostring(m.id) == tostring(id) then return m end
    end
    return nil
end

function Menu_Mods_Select(id)
    mlog('select: ' .. tostring(id))
    ttmod_menu_refresh()
    local m = Menu_Mods_Find(id)
    if m == nil then return end
    if Menu_Create == nil or Menu_Add == nil or Menu_Push == nil then
        mlog('select: engine menu globals missing, aborting')
        return
    end
    local menu = Menu_Create(ListMenu, 'ui_menu_options')
    if menu == nil then mlog('select: create failed') return end
    menu.align = 'left'
    menu.background = {}
    menu.Populate = function(self)
        local h = Menu_Add(Header, nil, 'header_settings')
        setlabel(h, m.name)
        local ver = Menu_Add(ListButton, 'ver', 'label_OK', 'Menu_Mods_Noop()')
        setlabel(ver, 'Version ' .. tostring(m.version))
        local st = m.enabled and 'ON' or 'OFF'
        local en = Menu_Add(ListButton, 'enabled', 'label_OK',
            'Menu_Mods_Toggle("' .. cbquote(m.id) .. '")')
        setlabel(en, 'Enabled: ' .. st)
        if m.config ~= nil then
            for _, c in ipairs(m.config) do
                local row = Menu_Add(ListButton, 'cfg_' .. tostring(c.key), 'label_OK',
                    'Menu_Mods_Adjust("' .. cbquote(m.id) .. '","' .. cbquote(c.key) .. '")')
                setlabel(row, tostring(c.label) .. ': ' .. tostring(c.value))
            end
        end
        local hint = Menu_Add(ListButton, 'hint', 'label_OK', 'Menu_Mods_Noop()')
        setlabel(hint, 'Restart required')
        local b = Menu_Add(ListButton, 'back', 'label_OK', 'Menu_Pop()')
        setlabel(b, 'Back')
        mlog('populate: details done')
    end
    Menu_Push(menu)
end

-- Palette for "color" config rows. 16 hand-picked #RRGGBB values (rows run
-- dark->bright per hue family, so the grid reads as a value scale). Chosen
-- rather than computed: a computed grid needs RGB math in the game's Lua and
-- gives worse-looking results than these.
-- ponytail: fixed 16-swatch list. The engine renders a FIXED number of
-- ListButton rows per menu; rows past that are added but render blank, so the
-- palette is paginated by TT_COLOR_PAGE to stay inside the measured capacity.
local TT_COLOR_SWATCHES = {
    '#FFFFFF', '#C0C0C0', '#808080', '#404040',
    '#FFE0A0', '#FFB000', '#FF8000', '#C04000',
    '#C0FFA0', '#00E000', '#008000', '#004000',
    '#A0C0FF', '#0080FF', '#0000C0', '#000040',
}
local TT_COLOR_PAGE = 6

local function is_hex(s)
    return type(s) == 'string' and s:match('^#%x%x%x%x%x%x$') ~= nil
end

-- Color row -> palette screen, one page of TT_COLOR_PAGE swatches at a time
-- (the engine renders a fixed row count; see TT_COLOR_PAGE). Picking a swatch
-- writes the value via the framework setter (which validates #RRGGBB
-- server-side and logs a rejection if it ever disagrees) and returns to the
-- details screen.
function Menu_Mods_PickColor(id, key, page)
    page = tonumber(page) or 1
    mlog('color: pick ' .. tostring(id) .. '.' .. tostring(key) .. ' page=' .. tostring(page))
    ttmod_menu_refresh()
    if Menu_Create == nil or Menu_Add == nil or Menu_Push == nil then
        mlog('color: engine menu globals missing, aborting')
        return
    end
    local m = Menu_Mods_Find(id)
    if m == nil then return end
    local cur = nil
    for _, c in ipairs(m.config or {}) do
        if tostring(c.key) == tostring(key) then cur = tostring(c.value) end
    end
    local menu = Menu_Create(ListMenu, 'ui_menu_options')
    if menu == nil then mlog('color: create failed') return end
    menu.align = 'left'
    menu.background = {}
    menu.Populate = function(self)
        local h = Menu_Add(Header, nil, 'header_settings')
        setlabel(h, tostring(key) .. ' color')
        local first = (page - 1) * TT_COLOR_PAGE + 1
        local last = first + TT_COLOR_PAGE - 1
        if last > #TT_COLOR_SWATCHES then last = #TT_COLOR_SWATCHES end
        for i = first, last do
            local hex = TT_COLOR_SWATCHES[i]
            local mark = (hex == cur) and ' *' or ''
            local r = Menu_Add(ListButton, 'sw_' .. tostring(i), 'label_OK',
                'Menu_Mods_SetColor("' .. cbquote(id) .. '","' .. cbquote(key) .. '","' ..
                hex .. '")')
            -- setlabel returns the label agent it used, so the swatch paints
            -- the same clone the hex text went to.
            paint(setlabel(r, hex .. mark), hex)
        end
        if page > 1 then
            local p = Menu_Add(ListButton, 'prevpage', 'label_OK',
                'Menu_Mods_PickColor("' .. cbquote(id) .. '","' .. cbquote(key) .. '",' ..
                tostring(page - 1) .. ')')
            setlabel(p, '<< Previous')
        end
        if last < #TT_COLOR_SWATCHES then
            local p = Menu_Add(ListButton, 'nextpage', 'label_OK',
                'Menu_Mods_PickColor("' .. cbquote(id) .. '","' .. cbquote(key) .. '",' ..
                tostring(page + 1) .. ')')
            setlabel(p, 'Next >>')
        end
        local b = Menu_Add(ListButton, 'back', 'label_OK', 'Menu_Pop()')
        setlabel(b, 'Back')
        mlog('populate: color grid done')
    end
    Menu_Push(menu)
    mlog('color: pushed grid rows=' .. tostring(Menu_Mods_RowCount()))
end

function Menu_Mods_SetColor(id, key, hex)
    if not is_hex(hex) then
        mlog('color: rejecting non-hex ' .. tostring(hex))
        return
    end
    ttmod_menu_set_value(tostring(id), tostring(key), hex)
    mlog('color: set ' .. tostring(id) .. '.' .. tostring(key) .. '=' .. hex)
    Menu_Pop()
    Menu_Mods_Select(id)
end

function Menu_Mods_Toggle(id)
    local m = Menu_Mods_Find(id)
    if m == nil then return end
    if m.enabled then
        ttmod_menu_set_enabled(tostring(id), '0')
    else
        ttmod_menu_set_enabled(tostring(id), '1')
    end
    Menu_Pop()
    Menu_Mods_Select(id)
end

function Menu_Mods_Adjust(id, key)
    local m = Menu_Mods_Find(id)
    if m == nil or m.config == nil then return end
    for _, c in ipairs(m.config) do
        if tostring(c.key) == tostring(key) then
            local t = tostring(c.type)
            if t == 'bool' then
                if c.value then
                    ttmod_menu_set_value(tostring(id), tostring(key), '0')
                else
                    ttmod_menu_set_value(tostring(id), tostring(key), '1')
                end
            elseif t == 'enum' then
                local opts = c.options
                local cur = tostring(c.value)
                local nxt = nil
                if opts ~= nil then
                    for k, v in ipairs(opts) do
                        if tostring(v) == cur then
                            nxt = opts[(k % #opts) + 1]
                            break
                        end
                    end
                    if nxt == nil and #opts > 0 then nxt = opts[1] end
                end
                if nxt ~= nil then
                    ttmod_menu_set_value(tostring(id), tostring(key), tostring(nxt))
                end
            elseif t == 'int' or t == 'float' then
                local step = tonumber(c.step) or 1
                local mn = tonumber(c.min)
                local mx = tonumber(c.max)
                local v = tonumber(c.value) or 0
                v = v + step
                if mx ~= nil and v > mx then v = (mn ~= nil and mn or 0) end
                ttmod_menu_set_value(tostring(id), tostring(key), tostring(v))
            elseif t == 'color' then
                Menu_Mods_PickColor(id, key, 1)
                return
            else
                Menu_Mods_EditString(id, key)
                return
            end
            break
        end
    end
    Menu_Pop()
    Menu_Mods_Select(id)
end
