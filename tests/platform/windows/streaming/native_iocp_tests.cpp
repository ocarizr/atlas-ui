/** @file @brief Actual Windows cancellation adapter exercised without depending on disk speed. */
#include "../../../streaming/temporary_assets.hpp"
#include "controlled_iocp.hpp"
#include <chrono>

namespace atlas::streaming::tests
{
/// @brief Stop after OS admission but before polling, so CancelIoEx is always attempted.
TEST_F(iocp_test, native_cancel_drains_actual_overlapped_read_even_when_completion_wins)
{
    temporary_assets assets;
    assets.write();
    backend = detail::make_file_io();
    std::stop_source stop;
    EXPECT_FALSE(backend->start(operation(4, stop.get_token()), assets.path()));
    stop.request_stop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    std::optional<detail::file_completion> done;
    do
    {
        done = backend->poll(true);
    } while (!done && std::chrono::steady_clock::now() < deadline);

    ASSERT_TRUE(done);
    expect_cancelled(require_result(std::move(done)));
    EXPECT_EQ(backend->statistics().cancellation_requests, 1);
    EXPECT_EQ(backend->statistics().cancellation_errors, 0);
    EXPECT_FALSE(backend->poll(false));
}
} // namespace atlas::streaming::tests
