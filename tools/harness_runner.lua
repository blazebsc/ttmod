-- Harness runner: executes a build_harness.py chunk with a stub _ENV.
-- Every unknown global is a self-similar logging stub (callable, indexable,
-- never errors on call/index/compare; arithmetic on stubs WILL error, which
-- surfaces as a pcall failure naming the line - that is diagnostic data).
local calls = {}
-- Telltale's Lua keeps 5.1-isms absent from stock 5.2: shim them.
table.getn = table.getn or function(t) return #t end
local function mkstub(path)
    local t = {}
    setmetatable(t, {
        __index = function(_, k)
            return mkstub(path .. "." .. tostring(k))
        end,
        __call = function(_, ...)
            local parts = {}
            for i = 1, math.min(select("#", ...), 5) do
                local a = select(i, ...)
                if type(a) == "string" then
                    parts[#parts + 1] = string.format("%q", a)
                else
                    parts[#parts + 1] = type(a)
                end
            end
            calls[#calls + 1] = path .. "(" .. table.concat(parts, ", ") .. ")"
            return mkstub(path .. "()")
        end,
        __tostring = function() return "stub(" .. path .. ")" end,
    })
    return t
end

local chunk = assert(loadfile(arg[1], "b"))
-- Controlled fakes for known engine globals (neutral values keep execution
-- moving past string/arithmetic/error-prone operations).
local fakes = {
    SaveLoad_GetSlotDisplayName = function() return "" end,
}
local env = setmetatable({}, {
    __index = function(_, k)
        if fakes[k] ~= nil then return fakes[k] end
        local v = rawget(_G, k)
        if v ~= nil then return v end
        return mkstub(tostring(k))
    end,
    __newindex = function() end,
})
debug.setupvalue(chunk, 1, env)
local ok, err = pcall(chunk)
print("pcall ok=" .. tostring(ok) .. (ok and "" or (" err=" .. tostring(err))))
local found = 0
local found_mods = false
for _, c in ipairs(calls) do
    if c:sub(1, 9) == "Menu_Add(" then
        found = found + 1
        if c:find('"mods"') then
            found_mods = true
            print("  MODS CALL: " .. c)
        end
    end
end
print("Menu_Add calls: " .. found)
print("Mods Menu_Add call: " .. tostring(found_mods))
if not found_mods then os.exit(1) end
