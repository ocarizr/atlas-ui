-- A deterministic developer fixture; no project-specific gameplay or shipped content.
local function increment(value)
    return value + 1
end

return {
    tick = function()
        local answer = 41
        answer = increment(answer)
        assert(answer == 42)
    end,
}
