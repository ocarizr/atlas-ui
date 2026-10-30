#include "job_system_fixture.hpp"

#include <atomic>
#include <vector>

namespace atlas::jobs::tests
{
constexpr auto stateless = [](job_context&, int&) {};
constexpr auto captured = [value = 1](job_context&, int& data) { data += value; };
static_assert(job_function<decltype(stateless), int>);
static_assert(!job_function<decltype(captured), int>);

TEST_F(job_system_fixture, context_only_jobs_do_not_require_dummy_payloads)
{
    auto jobs = system.dispatcher();
    auto root = jobs.run("parent",
                         [](job_context& context) { context.spawn("child", [](job_context&) {}); });
    jobs.after(root, "next", [](job_context&) {}).wait();
    EXPECT_EQ(system.statistics().completed, 3U);
}

TEST_F(job_system_fixture, direct_handles_sequence_jobs_without_a_graph)
{
    auto jobs = system.dispatcher();
    auto value = std::make_shared<int>(0);
    const auto first = jobs.run("produce", value, [](job_context&, auto& data) { *data = 21; });
    const auto next =
        jobs.after(first, "transform", value, [](job_context&, auto& data) { *data *= 2; });
    jobs.after(next, "consume", value, [](job_context&, auto& data) { EXPECT_EQ(*data, 42); })
        .wait();
    EXPECT_EQ(next.status(), job_status::succeeded);
    EXPECT_EQ(next.name(), "transform");
    EXPECT_FALSE(next.exception());
    EXPECT_NO_THROW(next.wait());
}

TEST_F(job_system_fixture, fan_in_and_fences_observe_all_results)
{
    auto jobs = system.dispatcher();
    auto total = std::make_shared<std::atomic<int>>(0);
    std::vector<task_handle> prerequisites;
    prerequisites.reserve(65);
    for (int index = 0; index < 64; ++index)
    {
        prerequisites.push_back(jobs.run("add", total, [](job_context&, auto& data) { ++*data; }));
    }
    prerequisites.push_back(prerequisites.front());
    jobs.after(prerequisites, "join", total,
               [](job_context&, auto& data) { EXPECT_EQ(data->load(), 64); })
        .wait();
    jobs.fence("empty", {}).wait();
    EXPECT_EQ(system.statistics().submitted, 66U);
}

TEST_F(job_system_fixture, move_only_data_is_owned_and_the_context_is_injected)
{
    struct input
    {
        std::unique_ptr<int> value;
        std::shared_ptr<std::atomic<int>> output;
    };
    auto output = std::make_shared<std::atomic<int>>(0);
    system.dispatcher()
        .run("owned", input{std::make_unique<int>(42), output},
             [](job_context&, input& data) { *data.output = *data.value; })
        .wait();
    EXPECT_EQ(output->load(), 42);
    // Async unique_ptr ownership escapes analyzer modeling; payload cleanup is tested separately.
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

TEST_F(job_system_fixture, invalid_dependencies_are_rejected_before_submission)
{
    auto jobs = system.dispatcher();
    job_system other{1};
    auto foreign = other.dispatcher().run("foreign", 0, stateless);
    EXPECT_THROW(jobs.after(task_handle{}, "invalid", 0, stateless), std::invalid_argument);
    EXPECT_THROW(jobs.after(foreign, "foreign", 0, stateless), std::invalid_argument);
    EXPECT_THROW(jobs.run("", 0, stateless), std::invalid_argument);
    EXPECT_EQ(system.statistics().submitted, 0U);
}

TEST_F(job_system_fixture, failed_prerequisites_skip_callbacks_and_preserve_the_original_error)
{
    auto jobs = system.dispatcher();
    auto forbidden = std::make_shared<std::atomic<int>>(0);
    auto failed =
        jobs.run("failed", 0, [](job_context&, int&) { throw std::runtime_error{"bad data"}; });
    auto skipped =
        jobs.after(failed, "skipped", forbidden, [](job_context&, auto& data) { ++*data; });
    EXPECT_THROW(skipped.wait(), job_error);
    EXPECT_EQ(skipped.status(), job_status::skipped);
    EXPECT_EQ(forbidden->load(), 0);
    EXPECT_THROW(std::rethrow_exception(skipped.exception()), std::runtime_error);
    jobs.run("still alive", 0, stateless).wait();
}
} // namespace atlas::jobs::tests
