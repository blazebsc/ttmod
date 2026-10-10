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
-- Deliberate per-agent colours (the colour picker's palette rows). An agent
-- whose colour was chosen to be something other than the accent must KEEP
-- that colour through the engine's hover select/deselect cycle, which
-- otherwise repaints it accent and never restores it (2026-10-07: hovering a
-- colour option stuck it accent until game restart). paint() records it;
-- apply_theme and the write wrappers read it before choosing the accent.
local theme_custom = {}
-- Attribution: which screen painted the agent. Set in apply_theme from the
-- TTMOD_OWN_BUILD flag (true while OUR Populate runs inside Menu_Push -
-- synchronous, single-threaded). 'own' = our Mods screens (popped menus
-- destroy their agents: post-pop reads are corpses, not live overwrites);
-- 'menu' = game menus incl. the never-popped main menu (always live truth).
local theme_where = {}
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
    -- Record the intended colour BEFORE the write: the AgentSetProperty
    -- wrapper reads theme_custom to decide preserve-vs-accent, and a pale
    -- swatch's own near-gray-white paint must be preserved by this very
    -- write, not substituted to the accent.
    local prev_custom = theme_custom[agent]
    theme_custom[agent] = hex
    if not set_color(agent, theme_winner, hex) then
        theme_custom[agent] = prev_custom
        return false
    end
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
    -- Deliberate custom colour (palette swatch): keep it. Every repaint path
    -- - the rollover wrapper, the retry queue, TTMOD_THEME_WIDGET - lands
    -- here, so this one check preserves the swatch through the whole hover
    -- select/deselect cycle instead of sticking it accent.
    local custom = theme_custom[agent]
    if custom ~= nil and custom ~= acc then
        if theme_painted[agent] == custom then return end
        if paint(agent, custom) then
            theme_painted[agent] = custom
            theme_where[agent] = (TTMOD_OWN_BUILD and 'own' or 'menu')
        end
        return
    end
    if theme_painted[agent] == acc then return end
    if paint(agent, acc) then
        theme_painted[agent] = acc
        theme_where[agent] = (TTMOD_OWN_BUILD and 'own' or 'menu')
    elseif not theme_probed then
        mlog('theme-winner: none')
        theme_probed = true
    end
end

-- Retry queue for unpopulated widgets (declared here: theme_audit reads it,
-- and declaration order is load-bearing in this file).
local TT_PENDING = {}
local TT_PENDING_MAX = 32

-- Read-only audit: proves WHERE the stuck-white comes from. Runs inside
-- theme_drain (every Menu_Add): re-reads Text Color on every painted agent
-- and compares numerically to the session accent. ALL engine calls pcall'd,
-- NEVER writes: discovery only, one log line per drain that finds an
-- overwrite. Dead agents (read throws or nil) are dropped, which also bounds
-- the table across menu rebuilds. Forward uses only: defined before
-- theme_drain (declaration order is load-bearing in this file).
--   holds        = property still accent -> the white is render-internal
--                  (only a native render detour could fix it, or accept it).
--   overwritten  = the engine WROTE a non-accent value into the property
--                  (a native AgentSetProperty filter can substitute accent
--                  at the same point - low-frequency, human-driven, no
--                  per-frame hook needed).
local function theme_audit()
    local acc = TTMOD_ACCENT
    if type(acc) ~= 'string' or AgentGetProperty == nil then return end
    local r0, g0, b0 = acc:match('^#(%x%x)(%x%x)(%x%x)$')
    if r0 == nil then return end
    local wr, wg, wb = tonumber(r0, 16) / 255, tonumber(g0, 16) / 255,
                       tonumber(b0, 16) / 255
    local n, over, sample = 0, 0, nil
    local over_own, over_menu = 0, 0
    -- Skip agents still awaiting paint: labels exist before the retry queue
    -- paints them, and auditing a not-yet-painted label reads template stock
    -- (0.878/white) - a false flag indistinguishable from an overwrite.
    -- (2026-10-04: every historical flag sampled template values; none ever
    -- sampled a mid-value no template holds.)
    local unpainted = {}
    for _, entry in ipairs(TT_PENDING) do
        local w = entry[1]
        if w ~= nil then
            local ok, ag = pcall(function()
                return (w.agent ~= nil) and w.agent or w
            end)
            if ok and ag ~= nil then unpainted[ag] = true end
        end
    end
    for agent in pairs(theme_painted) do
        if unpainted[agent] then
            theme_painted[agent] = nil
            theme_where[agent] = nil
        else
        local ok, v = pcall(AgentGetProperty, agent, 'Text Color')
        if not ok or v == nil then
            theme_painted[agent] = nil
            theme_where[agent] = nil
        elseif type(v) == 'table' and type(v.r) == 'number' then
            n = n + 1
            local function near(a, b) return math.abs(a - b) < 1e-4 end
            -- Expected colour is whatever THIS agent was painted with - the
            -- accent for themed rows, the swatch hex for palette rows - so a
            -- correct swatch never false-flags as an overwrite.
            local er, eg, eb = wr, wg, wb
            local want = theme_painted[agent]
            if type(want) == 'string' then
                local a1, a2, a3 = want:match('^#(%x%x)(%x%x)(%x%x)$')
                if a1 ~= nil then
                    er = tonumber(a1, 16) / 255
                    eg = tonumber(a2, 16) / 255
                    eb = tonumber(a3, 16) / 255
                end
            end
            if not (near(v.r, er) and near(v.g or 0, eg) and
                    near(v.b or 0, eb)) then
                over = over + 1
                if theme_where[agent] == 'own' then over_own = over_own + 1
                else over_menu = over_menu + 1 end
                if sample == nil then
                    sample = string.format('%.3f,%.3f,%.3f', v.r, v.g or 0,
                                           v.b or 0)
                end
            end
        else
            n = n + 1
        end
        end
    end
    if over > 0 then
        mlog('theme-audit: ' .. over .. ' of ' .. n ..
             ' painted labels read non-accent (own:' .. over_own ..
             ' menu:' .. over_menu .. ', e.g. ' .. (sample or '?') .. ')')
    end
end

-- Lua-level white-write filter (2026-10-04). The audit proved the stuck-white
-- is a real WRITE into Text Color (deselect restores stock 0.878 gray, select
-- writes white) - and the game's own UI scripts drive those writes through
-- this same Lua global (Clone_Find + AgentSetProperty is the documented UI
-- idiom). So wrap the global: near-gray-white writes become the accent,
-- everything else tail-calls the original untouched. Semantics-preserving:
-- same args, same return values, no recursion (the wrapper never calls the
-- global, only the saved original). Rule is deliberately narrow - only
-- near-gray-white (min channel >= 0.8, floats or 0..255 ints); disabled-gray
-- and deliberate tints pass through. Engine-internal native writes bypass Lua
-- entirely and are NOT caught here (theme-audit tells us if any remain).
-- Installs wherever this chunk loads while AgentSetProperty already exists,
-- at the top of TTMOD_THEME_WIDGET, and from the Menu_Add wrapper chunk at
-- Menu.lua load (earliest: catches game scripts that localize the global
-- before the first widget builds). Menu-states only; engine states that
-- lack Menu_Add stay untouched.
local function theme_hex_rgb(s)
    if type(s) ~= 'string' then return nil end
    local r, g, b = s:match('^#(%x%x)(%x%x)(%x%x)$')
    if r == nil then return nil end
    return { tonumber(r, 16) / 255, tonumber(g, 16) / 255,
             tonumber(b, 16) / 255 }
end
local function theme_accent_rgb()
    return theme_hex_rgb(TTMOD_ACCENT)
end
-- An agent's deliberate custom colour as a writeable value table, or nil.
local function theme_custom_rgb(agent)
    if agent == nil then return nil end
    local hex = theme_custom[agent]
    if hex == nil then return nil end
    local rgb = theme_hex_rgb(hex)
    if rgb == nil then return nil end
    return { r = rgb[1], g = rgb[2], b = rgb[3], a = 1 }
end
local function theme_substitute(prop, v)
    if prop ~= 'Text Color' or type(v) ~= 'table' then return nil end
    local r, g, b = v.r, v.g, v.b
    if type(r) ~= 'number' or type(g) ~= 'number' or
       type(b) ~= 'number' then
        return nil
    end
    local scale = 1
    if r > 1 or g > 1 or b > 1 then scale = 255 end
    local mn, mx = r, r
    if g < mn then mn = g end
    if b < mn then mn = b end
    if g > mx then mx = g end
    if b > mx then mx = b end
    -- Same rule as core should_substitute (tests/test_themecolor.cpp holds
    -- the golden vectors; mirror them in the suite below when changing).
    if mx / scale - mn / scale >= 0.05 then return nil end
    if mn / scale < 0.8 then return nil end
    local rgb = theme_accent_rgb()
    if rgb == nil then return nil end
    return { r = rgb[1], g = rgb[2], b = rgb[3],
             a = (type(v.a) == 'number' and v.a or 1) }
end
-- Menu_Pop wrapper (2026-10-09): any pop off the colour picker must clear
-- the palette glow-suppression flag. The Back button's callback is the Lua
-- string 'Menu_Pop()', so redefining the global intercepts engine-driven
-- pops too - the same global-wrapper pattern as theme_wrap_asp. Load-safe:
-- installed from TTMOD_THEME_WIDGET only, when Menu_Pop actually exists.
local function theme_wrap_pop()
    if ttmod_pop_wrapped then return end
    if type == nil or Menu_Pop == nil then return end
    if type(Menu_Pop) ~= 'function' then return end
    local orig = Menu_Pop
    Menu_Pop = function(...)
        if ttmod_menu_palette ~= nil then pcall(ttmod_menu_palette, '0') end
        return orig(...)
    end
    ttmod_pop_wrapped = true
end
local function theme_wrap_asp()
    -- Load-safe: this file's top level must NEVER call globals. The chunk
    -- runs at lua_newstate capture, before the engine opens standard libs
    -- (2026-10-04: a load-time type() call killed the whole chunk with
    -- "attempt to call global 'type' (a nil value)", taking Menu_Mods and
    -- all painting with it). Install happens from TTMOD_THEME_WIDGET only.
    if ttmod_asp_wrapped then return end
    if type == nil or AgentSetProperty == nil then return end
    if type(AgentSetProperty) ~= 'function' then return end
    local orig = AgentSetProperty
    AgentSetProperty = function(agent, prop, v, ...)
        local sub = theme_substitute(prop, v)
        if sub ~= nil then
            local allow = TTMOD_THEME_SCOPE ~= 'ttmod' or
                (agent ~= nil and theme_painted[agent] ~= nil)
            if allow then
                -- Custom-coloured agent (palette swatch): the engine's
                -- select/deselect stock writes restore ITS colour, never the
                -- accent - that restore is what stuck swatches accent-only.
                local keep = theme_custom_rgb(agent)
                if keep ~= nil then
                    mlog('theme-sub: preserving custom colour')
                    return orig(agent, prop, keep)
                end
                mlog('theme-sub: Text Color stock/white -> accent')
                return orig(agent, prop, sub)
            end
        end
        return orig(agent, prop, v, ...)
    end
    ttmod_asp_wrapped = true
end

-- Rollover (hover) write filter (2026-10-04). Static analysis of the
-- unpacked image found the hover mechanism: Lua binding
-- RolloverEnableTextColor at RVA 0x73F730 (registration thunk pushes the
-- name + function address), which resolves Text Color natively and writes
-- through the engine setters - bypassing the AgentSetProperty global
-- entirely (that is why theme_wrap_asp saw zero writes while theme-audit
-- proved overwrites). Wrapping THIS global intercepts the hover path:
-- after the original runs, repaint the agent with the accent.
-- Same safety shape as theme_wrap_asp: load-safe guard (chunk runs before
-- libs open), tail-call semantics preserved via { } + unpack (plain unpack
-- exists in 5.1; table.unpack does not), scope-aware, cache-bypassing
-- repaint (the paint cache would otherwise skip the just-overwritten agent).
local function theme_repaint_agent(agent)
    local acc = TTMOD_ACCENT
    if type(acc) ~= 'string' then return end
    if agent == nil then return end
    theme_painted[agent] = nil
    apply_theme(agent)
    if Clone_Find ~= nil then
        for _, child in ipairs({ 'label', 'caption', 'text', 'ui_listButton_label',
                                 'ui_header_header', 'ui_listButton_button' }) do
            pcall(function()
                local okc, c = pcall(Clone_Find, agent, child)
                if okc and c ~= nil then
                    theme_painted[c] = nil
                    apply_theme(c)
                end
            end)
        end
    end
end
local function theme_wrap_roll()
    if ttmod_roll_wrapped then return end
    if type == nil or RolloverEnableTextColor == nil then return end
    if type(RolloverEnableTextColor) ~= 'function' then return end
    local orig = RolloverEnableTextColor
    RolloverEnableTextColor = function(agent, enable, ...)
        local t = { orig(agent, enable, ...) }
        if type(TTMOD_ACCENT) == 'string' then
            local allow = TTMOD_THEME_SCOPE ~= 'ttmod' or
                (agent ~= nil and theme_painted[agent] ~= nil)
            if allow then
                mlog('theme-roll: rollover repaint')
                pcall(theme_repaint_agent, agent)
            end
        end
        return unpack(t)
    end
    ttmod_roll_wrapped = true
end

-- TextSetColor wrapper (2026-10-04). The registry also exposes TextSetColor
-- (binding 0x730690): if game scripts drive hover through it instead of
-- RolloverEnableTextColor, this is where the white comes from. Same shape as
-- the ASP wrapper but for NUMBER args (agent, r, g, b[, a]) as well as a
-- table first value; near-gray-white becomes the accent, all else passes.
-- One variable at a time: background/mesh rollover bindings stay untouched.
local function theme_wrap_tc()
    if ttmod_tc_wrapped then return end
    if type == nil or TextSetColor == nil then return end
    if type(TextSetColor) ~= 'function' then return end
    local orig = TextSetColor
    TextSetColor = function(agent, r, g, b, a, ...)
        local v = nil
        if type(r) == 'table' then
            v = r
        elseif type(r) == 'number' and type(g) == 'number' and
               type(b) == 'number' then
            v = { r = r, g = g, b = b, a = a }
        end
        if v ~= nil then
            local sub = theme_substitute('Text Color', v)
            if sub ~= nil then
                local allow = TTMOD_THEME_SCOPE ~= 'ttmod' or
                    (agent ~= nil and theme_painted[agent] ~= nil)
                if allow then
                    local keep = theme_custom_rgb(agent)
                    if keep ~= nil then
                        mlog('theme-tc: preserving custom colour')
                        if type(r) == 'table' then
                            return orig(agent, keep)
                        end
                        return orig(agent, keep.r, keep.g, keep.b, a)
                    end
                    local rgb = theme_accent_rgb()
                    mlog('theme-tc: TextSetColor stock/white -> accent')
                    if type(r) == 'table' then
                        return orig(agent, sub)
                    end
                    if r > 1 or g > 1 or b > 1 then
                        return orig(agent, rgb[1] * 255, rgb[2] * 255,
                                    rgb[3] * 255, a)
                    end
                    return orig(agent, rgb[1], rgb[2], rgb[3], a)
                end
            end
        end
        return orig(agent, r, g, b, a, ...)
    end
    ttmod_tc_wrapped = true
end

-- Rollover-family LOG-ONLY wrappers (2026-10-05). The white may come from
-- a separate highlight overlay (RolloverEnableRolloverMesh) rather than the
-- painted label: root has 3 children, only 2 named. These wrappers NEVER
-- modify (tail-call verbatim); they prove which rollover traffic fires on
-- hover. Skip-vs-keep is decided from the log, next round.
local function theme_wrap_rolfam()
    if ttmod_rolfam_wrapped then return end
    if type == nil then return end
    local names = { 'RolloverEnableRolloverMesh', 'RolloverEnableTextBackgroundColor',
                    'RolloverResetStatus' }
    local any = false
    for _, nm in ipairs(names) do
        local orig = nil
        if nm == 'RolloverEnableRolloverMesh' then orig = RolloverEnableRolloverMesh
        elseif nm == 'RolloverEnableTextBackgroundColor' then orig = RolloverEnableTextBackgroundColor
        else orig = RolloverResetStatus end
        if type(orig) == 'function' then
            any = true
            local nmc, objc = nm, orig
            if nmc == 'RolloverEnableRolloverMesh' then
                RolloverEnableRolloverMesh = function(a, b, ...)
                    mlog('theme-mesh: ' .. tostring(b))
                    return objc(a, b, ...)
                end
            elseif nmc == 'RolloverEnableTextBackgroundColor' then
                RolloverEnableTextBackgroundColor = function(a, b, ...)
                    mlog('theme-bg: ' .. tostring(b))
                    return objc(a, b, ...)
                end
            else
                RolloverResetStatus = function(...)
                    mlog('theme-resetstatus')
                    return objc(...)
                end
            end
        else
            mlog('theme-rolfam-missing: ' .. nm)
        end
    end
    if any then ttmod_rolfam_wrapped = true end
end

-- Global entry for the C++ Menu_Add wrapper chunk (a separate chunk only
-- sees globals): installs all theme wrappers at Menu.lua load, before game
-- scripts can localize the originals. Idempotent per state. (2026-10-04:
-- the earlier literal line referenced theme_wrap_asp, a local - always nil
-- as a global, so early install never ran. This entry fixes that.)
function TTMOD_THEME_WRAP()
    theme_wrap_asp()
    theme_wrap_roll()
    theme_wrap_tc()
    theme_wrap_rolfam()
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
    -- CHILD enumeration (2026-10-05): the paint covers 6 fixed child names.
    -- A 7th text-bearing clone would escape paint AND audit (both use the
    -- same list) while rendering white. AgentGetChild(s) exists in the
    -- registry (0x77F060/0x77F4E0) - ask it, read-only, all shapes pcall'd.
    for _, cf in ipairs({ 'AgentGetChildren', 'AgentGetChild' }) do
        local cfn = nil
        if cf == 'AgentGetChildren' then cfn = AgentGetChildren
        else cfn = AgentGetChild end
        if cfn ~= nil then
            for _, sh in ipairs({ '(agent)', '(agent,true)' }) do
                local arg2 = (sh == '(agent,true)')
                local ok, r1, r2, r3 = nil, nil, nil, nil
                if arg2 then ok, r1, r2, r3 = pcall(cfn, agent, true)
                else ok, r1, r2, r3 = pcall(cfn, agent) end
                if ok then
                    for ri, rv in ipairs({ r1, r2, r3 }) do
                        local tv = type(rv)
                        if tv == 'number' then
                            mlog('sweep-' .. tag .. '-kids: ' .. cf .. sh ..
                                 ' count=' .. tostring(rv))
                        elseif tv == 'table' then
                            local nk = 0
                            local vals = {}
                            for k2, v2 in pairs(rv) do
                                nk = nk + 1
                                if #vals < 4 then
                                    local t2 = type(v2)
                                    if t2 == 'string' or t2 == 'number' or
                                       t2 == 'boolean' then
                                        vals[#vals + 1] = tostring(k2) .. '=' ..
                                            tostring(v2)
                                    elseif t2 == 'userdata' then
                                        -- Name the child agent if possible.
                                        local nm = '?'
                                        if AgentGetName ~= nil then
                                            local okn, nmv = pcall(AgentGetName, v2)
                                            if okn and type(nmv) == 'string' then
                                                nm = nmv
                                            end
                                        end
                                        vals[#vals + 1] = tostring(k2) ..
                                            '=agent(' .. nm .. ')'
                                    else
                                        vals[#vals + 1] = tostring(k2) .. '=<' ..
                                            t2 .. '>'
                                    end
                                end
                            end
                            mlog('sweep-' .. tag .. '-kids: ' .. cf .. sh ..
                                 ' table n=' .. nk .. ' ' ..
                                 table.concat(vals, ' '))
                        elseif tv == 'string' then
                            mlog('sweep-' .. tag .. '-kids: ' .. cf .. sh ..
                                 ' str=' .. string.sub(rv, 1, 80))
                        elseif tv == 'userdata' then
                            mlog('sweep-' .. tag .. '-kids: ' .. cf .. sh ..
                                 ' opaque-userdata')
                        end
                    end
                end
            end
        else
            mlog('sweep-' .. tag .. '-kids: ' .. cf .. ' missing')
        end
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
    theme_audit()
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
    -- its common children, which is where the visible text lives.
    -- NOTE: ui_listButton_button is the button clone; it owns the engine's
    -- hover/selection fill = 'Selection Color'. Sweeping read it back as
    -- stock green on the button even after the rest of the widget was themed,
    -- which is why hover snapped the row to white. Include it so paint()
    -- reaches its Selection Color too (2026-10-04).
    local found_any = false
    if Clone_Find ~= nil then
        for _, child in ipairs({ 'label', 'caption', 'text', 'ui_listButton_label',
                                 'ui_header_header', 'ui_listButton_button' }) do
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
    theme_wrap_pop()
    theme_wrap_asp()
    theme_wrap_roll()
    theme_wrap_tc()
    theme_wrap_rolfam()
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
    theme_where = {}
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
    if ttmod_menu_palette ~= nil then pcall(ttmod_menu_palette, '0') end
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
    -- Own-build window: Populate runs synchronously inside Menu_Push
    -- (single thread), so everything painted here tags as 'own'.
    TTMOD_OWN_BUILD = true
    Menu_Push(menu)
    TTMOD_OWN_BUILD = nil
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
    if ttmod_menu_palette ~= nil then pcall(ttmod_menu_palette, '0') end
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
    -- Own-build window: Populate runs synchronously inside Menu_Push
    -- (single thread), so everything painted here tags as 'own'.
    TTMOD_OWN_BUILD = true
    Menu_Push(menu)
    TTMOD_OWN_BUILD = nil
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
    -- Swatch rows captured for the post-push repaint below: the engine
    -- applies each row's template during its realization pass, which runs
    -- AFTER Populate returns - everything painted inside Populate is
    -- overwritten (the in-session audit read every swatch back as template
    -- 0.878). Repainting AFTER Menu_Push lands on the final, stable widget
    -- state - the same state the accent holds on every other screen.
    local painted = {}
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
            -- Paint the swatch colour on the row's LABEL and its BUTTON
            -- agent. The engine renders a selected row from the button's
            -- colour slots, and TTMOD_THEME_WIDGET paints that button accent
            -- at Menu_Add time - so painting only the label leaves the
            -- selected-state render sourced from accent.
            local lab = setlabel(r, hex .. mark)
            paint(lab, hex)
            if r ~= nil and r.agent ~= nil then paint(r.agent, hex) end
            painted[#painted + 1] = { lab, r, hex }
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
    -- Gate open BEFORE the push: the global default-colour register goes
    -- WHITE (identity) and stays white for the whole visit - there is no
    -- phase-2. White restores are suppressed natively while the gate is
    -- open, so the swatch colours painted here survive hover AND unhover.
    -- Cleared by the Menu_Pop wrapper and by every other screen builder
    -- below (register back to the accent, suppression off).
    if ttmod_menu_palette ~= nil then pcall(ttmod_menu_palette, '1') end
    -- Own-build window: Populate runs synchronously inside Menu_Push
    -- (single thread), so everything painted here tags as 'own'.
    TTMOD_OWN_BUILD = true
    Menu_Push(menu)
    -- Post-push repaint: the engine's realization pass overwrites
    -- everything painted inside Populate, so repaint the SWATCH colours
    -- here, on the final widget state. The register stays WHITE for the
    -- whole visit (no phase-2): unhover restores are suppressed natively,
    -- so nothing rewrites the rows after this lands.
    for _, e in ipairs(painted) do
        if e[1] ~= nil then paint(e[1], e[3]) end
        local ag = (e[2] ~= nil and e[2].agent ~= nil) and e[2].agent or e[2]
        if ag ~= nil then paint(ag, e[3]) end
    end
    -- Post-push read-back (bounded: 6 lines per push): proves the repaint
    -- landed. want=read on every row means the rest state is correct;
    -- anything after that is the hover/restore cycle, covered natively.
    if AgentGetProperty ~= nil then
        for i, e in ipairs(painted) do
            if e[1] ~= nil then
                pcall(function()
                    local ok, v = pcall(AgentGetProperty, e[1], 'Text Color')
                    local cur = '?'
                    if ok and type(v) == 'table' then
                        cur = string.format('%.3f,%.3f,%.3f', v.r or -1, v.g or -1, v.b or -1)
                    end
                    mlog('color: verify sw_' .. tostring(i) .. ' want=' .. tostring(e[3]) .. ' read=' .. cur)
                end)
            end
        end
    end
    TTMOD_OWN_BUILD = nil
    mlog('color: pushed grid rows=' .. tostring(Menu_Mods_RowCount()) ..
         ' repainted=' .. tostring(#painted))
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

-- Load-time self-test (keep LAST in file). A load-time error aborts the
-- whole chunk, killing Menu_Mods with the pcall code as the only symptom
-- (2026-10-04: a load-time type() call did exactly this). This line is the
-- all-clear: every screen entry point plus the theme entry points defined.
-- No type()/library calls: libs may not exist yet at capture time. Missing
-- names are logged by name so the next failure points at itself.
do
    local want = { 'Menu_Mods', 'Menu_Mods_Show', 'Menu_Mods_Select',
        'Menu_Mods_PickColor', 'Menu_Mods_Toggle', 'Menu_Mods_Adjust',
        'Menu_Mods_RowCount', 'TTMOD_THEME_WIDGET', 'TTMOD_THEME_WRAP' }
    local have = { Menu_Mods, Menu_Mods_Show, Menu_Mods_Select,
        Menu_Mods_PickColor, Menu_Mods_Toggle, Menu_Mods_Adjust,
        Menu_Mods_RowCount, TTMOD_THEME_WIDGET, TTMOD_THEME_WRAP }
    local n, missing = 0, nil
    for i = 1, #want do
        if have[i] ~= nil then n = n + 1
        elseif missing == nil then missing = want[i] end
    end
    mlog('menumods: ui self-test defs=' .. n .. '/' .. #want ..
         (missing and (' missing=' .. missing) or ''))
end
