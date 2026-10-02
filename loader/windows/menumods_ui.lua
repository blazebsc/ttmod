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

-- Optional menu theming (menu-theme mod sets _G.TTMOD_ACCENT = "#RRGGBB").
-- Absent = stock appearance (silent return, no log lines, zero behavior
-- change). Present = probe candidate color properties via pcall (failures
-- are silent engine-side); the first candidate the engine accepts wins for
-- the session. Only our Mods-menu labels are themed (via setlabel below);
-- game screens are never touched.
local theme_winner = nil
local theme_probed = false
-- Swatch preview cache: per-agent last paint, so re-rendering a row does not
-- re-probe or fight the engine's own refresh.
local theme_painted = setmetatable({}, {})
local function theme_int(s)
    if type(s) ~= 'string' then return nil end
    local r, g, b = s:match('^#(%x%x)(%x%x)(%x%x)$')
    if r == nil then return nil end
    return 255 * 16777216 + tonumber(r, 16) * 65536 + tonumber(g, 16) * 256 + tonumber(b, 16)
end
-- Paint `agent` with a specific hex. Returns true when the engine accepted it.
local function paint(agent, hex)
    if agent == nil or pcall == nil or AgentSetProperty == nil then return false end
    local num = theme_int(hex)
    -- The engine's AgentSetProperty may accept an unknown key and silently do
    -- nothing (pcall cannot see that), so a "winner" is only trusted when the
    -- value also READS BACK. Until a property proves itself the probe keeps
    -- trying the next candidate on each paint - cosmetic rows fall back to
    -- their hex text, which is always correct.
    local function readback(prop)
        if AgentGetProperty == nil then return nil end
        local ok, v = pcall(AgentGetProperty, agent, prop)
        if not ok then return nil end
        return v
    end
    local function same_color(a, b)
        if a == b then return true end
        local an, bn = theme_int(tostring(a)), theme_int(tostring(b))
        return an ~= nil and an == bn
    end
    local function try(prop)
        local ok = pcall(AgentSetProperty, agent, prop, hex)
        if not ok and num ~= nil then ok = pcall(AgentSetProperty, agent, prop, num) end
        if not ok then return false end
        local v = readback(prop)
        if v == nil then return false end -- unverifiable: keep probing
        return same_color(v, hex)
    end
    if theme_winner ~= nil then return try(theme_winner) end
    for _, prop in ipairs({'Color', 'Tint Color', 'Font Color', 'Text Color', 'Diffuse'}) do
        if try(prop) then
            theme_winner = prop
            mlog('theme-winner: ' .. prop)
            return true
        end
        mlog('theme-probe: ' .. prop .. '=fail')
    end
end
local function apply_theme(agent)
    if _G == nil or _G.TTMOD_ACCENT == nil then return end
    local acc = _G.TTMOD_ACCENT
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
            if ok2 then apply_theme(lab) return lab end
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
-- ponytail: fixed 16-swatch grid; add a computed HSL wheel only if mods want
-- arbitrary hues.
local TT_COLOR_SWATCHES = {
    '#FFFFFF', '#C0C0C0', '#808080', '#404040',
    '#FFE0A0', '#FFB000', '#FF8000', '#C04000',
    '#C0FFA0', '#00E000', '#008000', '#004000',
    '#A0C0FF', '#0080FF', '#0000C0', '#000040',
}

local function is_hex(s)
    return type(s) == 'string' and s:match('^#%x%x%x%x%x%x$') ~= nil
end

-- Color row -> palette screen (4x4). Picking a swatch writes the value via
-- the framework setter (which validates #RRGGBB server-side and logs a
-- rejection if it ever disagrees) and returns to the details screen.
function Menu_Mods_PickColor(id, key)
    mlog('color: pick ' .. tostring(id) .. '.' .. tostring(key))
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
        for i, hex in ipairs(TT_COLOR_SWATCHES) do
            local mark = (hex == cur) and ' *' or ''
            local r = Menu_Add(ListButton, 'sw_' .. tostring(i), 'label_OK',
                'Menu_Mods_SetColor("' .. cbquote(id) .. '","' .. cbquote(key) .. '","' ..
                hex .. '")')
            -- setlabel returns the label agent it used, so the swatch paints
            -- the same clone the hex text went to.
            paint(setlabel(r, hex .. mark), hex)
        end
        local b = Menu_Add(ListButton, 'back', 'label_OK', 'Menu_Pop()')
        setlabel(b, 'Back')
        mlog('populate: color grid done')
    end
    Menu_Push(menu)
    mlog('color: pushed grid')
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
                Menu_Mods_PickColor(id, key)
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
