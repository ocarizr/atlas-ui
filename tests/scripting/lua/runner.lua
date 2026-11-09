-- This trusted test runner owns LuaUnit and the fixture API in its own interpreter.
local unit, atlas, data_directory, report = ...
assert(unit.VERSION == "3.5", "Expected LuaUnit 3.5; run scripts.dev.setup_lua --tools luaunit")
assert(_VERSION == "Lua 5.5", "Expected the embedded Lua 5.5 runtime")

local function source(name)
    -- The trusted test runner reads fixture files. Behavior VMs never see this function.
    ---@diagnostic disable-next-line: undefined-global
    local file <close> = assert(io.open(data_directory .. "/" .. name .. ".lua", "rb"))
    return assert(file:read("*a"))
end

local tests = {}
for index = 5, select("#", ...) do
    local path = select(index, ...)
    local suite = assert(loadfile(path, "t"))
    for _, group in ipairs(suite(unit, atlas, source)) do
        tests[#tests + 1] = group
    end
end

local runner = unit.LuaUnit.new()
runner:runSuiteByInstances(tests, "--output", "junit", "--name", report)
assert(runner.result.runCount > 0, "LuaUnit executed zero tests")
return runner.result.notSuccessCount
