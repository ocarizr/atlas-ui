-- Tests run in LuaUnit. The loaded behaviors below run in the production domain profile.
local unit, atlas, source = ...
local counter = source("counter")
local clamp = source("clamp")

local function publish(side)
    atlas.publish(side, "clamp", 1, clamp)
    atlas.publish(side, "counter", 1, counter)
end

local function with_counter(test)
    atlas.with_fixture(function()
        publish("ui")
        local item = atlas.create("ui", "counter")
        atlas.begin_frame(1)
        test(item)
    end)
end

local pure = {}
local cases = {
    { "negative", -5, 0 },
    { "inside", 4, 4 },
    { "above_limit", 12, 10 },
}
for _, case in ipairs(cases) do
    pure["test_" .. case[1]] = function()
        local clamp_value = assert(load(clamp, "@clamp.lua", "t"))()
        unit.assertEquals(clamp_value(case[2], 10), case[3])
    end
end

local domain = {}

function domain.test_director_routes_in_registration_order()
    with_counter(function(first)
        local second = atlas.create("ui", "counter")
        atlas.call(first, "identify", "A")
        atlas.call(second, "identify", "B")
        atlas.call(first, "enable")
        atlas.synchronize("ui")
        atlas.dispatch("ui", "tick", 6)
        unit.assertEquals(atlas.read("ui", "count"), 0)
        atlas.flush("ui")
        unit.assertEquals({ atlas.call(first, "inspect") }, { 12, 12, "AB", 0 })
        unit.assertEquals({ atlas.call(second, "inspect") }, { 12, 12, "", 0 })
    end)
end

function domain.test_scoped_listeners_and_typed_action()
    with_counter(function(item)
        atlas.call(item, "enable")
        atlas.call(item, "send_action", 3)
        atlas.flush("ui")
        unit.assertEquals({ atlas.call(item, "inspect") }, { 0, 0, "", 0 })
        atlas.synchronize("ui")
        atlas.call(item, "send_action", 7)
        atlas.flush("ui")
        unit.assertEquals({ atlas.call(item, "inspect") }, { 0, 0, "", 7 })
        atlas.call(item, "disable")
        atlas.synchronize("ui")
        atlas.call(item, "send_action", 9)
        atlas.flush("ui")
        unit.assertEquals({ atlas.call(item, "inspect") }, { 0, 0, "", 7 })
        unit.assertErrorMsgContains("type", atlas.call, item, "send_action", "invalid")
    end)
end

function domain.test_reload_retires_listeners_only_after_admission()
    with_counter(function(item)
        atlas.call(item, "enable")
        atlas.synchronize("ui")
        atlas.publish("ui", "counter", 2, "invalid Lua")
        unit.assertErrorMsgContains("counter.lua", atlas.reload, item)
        atlas.call(item, "tick", 3)
        atlas.flush("ui")
        unit.assertEquals(atlas.read("ui", "mirror"), 3)

        atlas.publish("ui", "counter", 3, counter)
        local replacement = atlas.reload(item)
        unit.assertErrorMsgContains("stale", atlas.call, item, "tick", 3)
        atlas.call(replacement, "tick", 3)
        atlas.flush("ui")
        unit.assertEquals({ atlas.call(replacement, "inspect") }, { 6, 3, "", 0 })
    end)
end

function domain.test_messages_admit_only_owned_values_in_the_current_frame()
    with_counter(function(item)
        publish("application")
        local application = atlas.create("application", "counter")
        atlas.call(application, "tick", 8)
        atlas.flush("application")
        atlas.post(item, 1, 1, "receive", atlas.read("application", "count"))
        unit.assertErrorMsgContains("sequence", atlas.post, item, 1, 1, "receive", 9)
        unit.assertErrorMsgContains("frame", atlas.post, item, 2, 2, "receive", 9)
        unit.assertErrorMsgContains("scalar", atlas.post, item, 2, 1, "receive", {})
        atlas.deliver_messages("ui")
        unit.assertEquals({ atlas.call(item, "inspect") }, { 0, 0, "", 8 })
        unit.assertEquals(atlas.read("application", "count"), 8)
    end)
end

return { { "TestClamp", pure }, { "TestDomain", domain } }
