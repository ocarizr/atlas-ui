local unit, atlas, source = ...
local work = source("work")

local function counter()
    atlas.publish("ui", "clamp", 1, source("clamp"))
    atlas.publish("ui", "counter", 1, source("counter"))
    local item = atlas.create("ui", "counter")
    atlas.begin_frame(1)
    return item
end

local failures = {}
local cases = {
    { "assertion", "fail", "producer failed" },
    { "instruction_budget", "spin", "instruction budget exhausted" },
}
for _, case in ipairs(cases) do
    failures["test_" .. case[1] .. "_rolls_back_producer"] = function()
        atlas.with_fixture(function()
            local item = counter()
            atlas.call(item, "assert_restricted")
            unit.assertErrorMsgContains(case[3], atlas.call, item, case[2])
            atlas.flush("ui")
            unit.assertEquals(atlas.read("ui", "count"), 0)
            local replacement = atlas.reload(item)
            atlas.synchronize("ui")
            atlas.call(replacement, "tick", 4)
            atlas.flush("ui")
            unit.assertEquals({ atlas.call(replacement, "inspect") }, { 4, 0, "", 0 })
        end)
    end
end

function failures.test_failed_luaunit_assertion_unwinds_native_fixture()
    unit.assertErrorMsgContains("intentional assertion", atlas.with_fixture, function()
        local item = counter()
        atlas.call(item, "tick", 7)
        atlas.flush("ui")
        unit.assertEquals(atlas.read("ui", "count"), 0, "intentional assertion")
    end)
    atlas.with_fixture(function()
        local item = counter()
        unit.assertEquals({ atlas.call(item, "inspect") }, { 0, 0, "", 0 })
    end)
end

function failures.test_runner_coroutine_cannot_keep_a_stale_fixture_scope()
    local thread
    atlas.with_fixture(function()
        ---@diagnostic disable-next-line: undefined-global
        thread = coroutine.create(function()
            atlas.with_fixture(function()
                unit.assertEquals(atlas.read("ui", "count"), 0)
            end)
        end)
    end)
    ---@diagnostic disable-next-line: undefined-global
    local success, message = coroutine.resume(thread)
    unit.assertTrue(success, message)
end

local native_work = {}

function native_work.test_results_deliver_at_owner_boundary_in_submission_order()
    atlas.with_fixture(function()
        atlas.publish("ui", "work", 1, work)
        local item = atlas.create("ui", "work")
        atlas.begin_frame(1)
        atlas.call(item, "tick")
        unit.assertEquals(atlas.take_requests("ui"), 2)
        unit.assertTrue(atlas.complete(2, false, "second"))
        atlas.deliver_results("ui")
        unit.assertEquals(atlas.call(item, "inspect"), "")
        unit.assertTrue(atlas.complete(1, true, "first"))
        unit.assertFalse(atlas.complete(1, true, "duplicate"))
        unit.assertEquals(atlas.call(item, "inspect"), "")
        atlas.begin_frame(2)
        atlas.deliver_results("ui")
        unit.assertEquals(atlas.call(item, "inspect"), "+first-second")
    end)
end

function native_work.test_reload_rejects_late_completion()
    atlas.with_fixture(function()
        atlas.publish("ui", "work", 1, work)
        local item = atlas.create("ui", "work")
        atlas.begin_frame(1)
        atlas.call(item, "tick")
        unit.assertEquals(atlas.take_requests("ui"), 2)
        local replacement = atlas.reload(item)
        unit.assertFalse(atlas.complete(1, true, "stale"))
        atlas.deliver_results("ui")
        unit.assertEquals(atlas.call(replacement, "inspect"), "")
    end)
end

return { { "TestProtectedBehavior", failures }, { "TestNativeResults", native_work } }
