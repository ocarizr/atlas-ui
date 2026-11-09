local results = ""

return {
    tick = function()
        model.request("decode", "first")
        model.request("decode", "second")
    end,
    on_result = function(id, success, value)
        assert(type(id) == "number")
        results = results .. (success and "+" or "-") .. value
    end,
    inspect = function()
        return results
    end,
}
