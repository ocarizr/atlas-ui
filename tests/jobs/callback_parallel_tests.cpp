#include "job_system_fixture.hpp"

#include <algorithm>
#include <numeric>
#include <thread>
#include <vector>

namespace atlas::jobs::tests
{
struct callback_range_case
{
    std::size_t size;
    std::size_t grain;
};
std::ostream& operator<<(std::ostream& output, const callback_range_case& value)
{
    return output << "size=" << value.size << ", grain=" << value.grain;
}
class callback_parallel_tests : public testing::TestWithParam<callback_range_case>
{
};

TEST_P(callback_parallel_tests, success_notifications_observe_every_completed_chunk)
{
    job_system runtime{4};
    auto jobs = runtime.dispatcher();
    const auto parameters = GetParam();
    std::vector<int> values(parameters.size, 0);
    auto source =
        jobs.parallel_for("map", std::span{values}, parameters.grain,
                          [](job_context&, std::span<int>& chunk) { std::ranges::fill(chunk, 1); });
    struct input
    {
        std::span<const int> values;
        std::thread::id owner;
    };
    auto notification = jobs.callbacks().on_success(
        source, "consume", input{values, std::this_thread::get_id()},
        [](job_context&, const task_handle& result, input& data)
        {
            EXPECT_EQ(std::this_thread::get_id(), data.owner);
            EXPECT_EQ(result.status(), job_status::succeeded);
            EXPECT_EQ(std::accumulate(data.values.begin(), data.values.end(), std::size_t{0}),
                      data.values.size());
        });
    notification.wait();
}

INSTANTIATE_TEST_SUITE_P(boundaries, callback_parallel_tests,
                         testing::Values(callback_range_case{0, 8}, callback_range_case{1, 8},
                                         callback_range_case{8, 8}, callback_range_case{9, 8},
                                         callback_range_case{257, 16}));
} // namespace atlas::jobs::tests
