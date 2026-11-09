local clamp = require("clamp")
local count = model.field("count")
local mirror = model.field("mirror")
local signal = model.action("signal")
local changed = model.event("changed")
local tokens = {}
local order = ""
local name = ""
local received = 0

return {
    identify = function(label)
        name = label
    end,
    tick = function(value)
        model.add_integer(count, clamp(value, 10))
        model.emit(changed, name)
    end,
    enable = function()
        tokens = {
            model.subscribe(count, "count_changed"),
            model.subscribe(signal, "action_received"),
            model.subscribe(changed, "event_received"),
        }
    end,
    disable = function()
        for _, token in ipairs(tokens) do
            model.unsubscribe(token)
        end
    end,
    count_changed = function(value)
        model.set(mirror, value)
    end,
    action_received = function(value)
        received = value
    end,
    event_received = function(label)
        order = order .. label
    end,
    send_action = function(value)
        model.invoke(signal, value)
    end,
    receive = function(value)
        received = value
    end,
    inspect = function()
        return model.get(count), model.get(mirror), order, received
    end,
    fail = function()
        model.set(count, 99)
        model.subscribe(count, "count_changed")
        assert(false, "producer failed")
    end,
    spin = function()
        model.set(count, 99)
        while true do
        end
    end,
    assert_restricted = function()
        -- These globals are deliberately absent inside the production domain.
        ---@diagnostic disable-next-line: undefined-global
        assert(io == nil and os == nil and debug == nil and package == nil and pcall == nil)
        ---@diagnostic disable-next-line: undefined-global
        assert(atlas_test == nil and luaunit == nil)
    end,
}
