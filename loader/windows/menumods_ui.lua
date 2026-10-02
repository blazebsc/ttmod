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
local TT_PROBE_DONE = false
local TT_PROP_CANDIDATES = {
    'Color', 'Tint Color', 'Font Color', 'Text Color', 'Diffuse', 'Colour',
    'Color Tint', 'Tint', 'TextColour', 'TextColor', 'FontColour', 'FontColor',
    'Label Color', 'Label Text Color', 'Foreground Color', 'Label Tint',
    'ColorModulate', 'Modulate Color', 'Emissive Color', 'Text Diffuse',
    'Color State Normal', 'ColorNormal', 'Text Color Normal', 'Font Color Normal',
    'Color Highlight', 'Color Pressed', 'Color Disabled', 'Color Disabled Text',
}
local theme_winner = nil
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
        if is_ours then
            theme_winner = prop   -- arms paint() for the rest of the session
            mlog('probe-winner: ' .. prop)
            return
        end
        -- A property that READS BACK exists, even if our string value didn't
        -- stick (it wanted a table). Adopt it as the winner and stop guessing:
        -- paint() then tries every format including {r,g,b}. Continuing to probe
        -- past a readable property is pointless - it is the only one that exists.
        if readable then
            theme_winner = prop
            mlog('probe-winner: ' .. prop .. ' (exists, value format differs)')
            return
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
    mlog('probe: end (no match)')
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
-- Paint `agent` with a specific hex, but ONLY with a property already proven by
-- probe_props. Until one exists this is a deliberate no-op: spraying unknown
-- property names at every label during a screen build is what killed the game
-- mid-menu on 2026-10-02. Discovery happens once, inside probe_props.
-- Value formats tried, cheapest first. Probing showed the label's real colour
-- property reads back as a TABLE, so {r,g,b} is included; the string and int
-- forms are kept because a different property/locale may want those.
local function paint(agent, hex)
    if agent == nil or pcall == nil or AgentSetProperty == nil then return false end
    if theme_winner == nil then return false end
    local r, g, b = hex:match('^#(%x%x)(%x%x)(%x%x)$')
    if r == nil then return false end
    local num = theme_int(hex)
    local ri, gi, bi = tonumber(r, 16), tonumber(g, 16), tonumber(b, 16)
    if pcall(AgentSetProperty, agent, theme_winner, hex) then return true end
    if pcall(AgentSetProperty, agent, theme_winner, { ri, gi, bi, 255 }) then return true end
    if pcall(AgentSetProperty, agent, theme_winner, { ri, gi, bi }) then return true end
    if num ~= nil and pcall(AgentSetProperty, agent, theme_winner, num) then return true end
    return false
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
