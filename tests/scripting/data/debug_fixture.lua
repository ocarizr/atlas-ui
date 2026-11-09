-- A deterministic developer fixture; no project-specific gameplay or shipped content.
local function increment(value)
    return value + 1
end

return {
    tick = function(seed)
        local answer = seed or 41
        answer = increment(answer)
        assert(answer == (seed or 41) + 1)
    end,
}
