/** @file @brief Shared native jobs transport immutable results; only owner phases enter Lua. */
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_scripting::tests
{
/// @brief Completion arrival order cannot reorder the owner's accepted request prefix.
TEST(ui_script_jobs, DeliversResultsInSubmissionOrder)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    scripting::session behavior{model};
    behavior.load(
        "local count=model.field('count'); return {tick=function() model.request('compute'); "
        "model.request('compute') end, on_result=function(id,ok,value) assert(ok); "
        "model.add_integer(count,value) end}");
    model->begin_frame(1);
    behavior.begin_frame(1);
    behavior.call("tick");
    auto work = behavior.take_requests();
    ASSERT_EQ(work.size(), 2);
    const std::array<mvvm::value, 1> value{std::int64_t{3}};
    EXPECT_TRUE(work[1].complete(value));
    behavior.deliver_results();
    EXPECT_EQ(model->inspect().commands, 0);
    jobs::job_system workers{1};
    auto done =
        workers.dispatcher().run("script compute", work[0],
                                 [](jobs::job_context&, auto& request)
                                 {
                                     const std::array<mvvm::value, 1> result{std::int64_t{3}};
                                     if (!request.complete(result))
                                     {
                                         throw std::runtime_error("unexpected stale work");
                                     }
                                 });
    done.wait();
    behavior.deliver_results();
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("count")), 6);
}
} // namespace atlas::ui_scripting::tests
