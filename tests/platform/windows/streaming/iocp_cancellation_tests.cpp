/** @file @brief Cancellation requests are distinct from kernel completion acknowledgment. */
#include "controlled_iocp.hpp"

namespace atlas::streaming::tests
{
/// @brief Real cancellation never frees a buffer while the OS may still write to it.
TEST_F(iocp_test, cancellation_retains_request_until_abort_acknowledgment)
{
    std::stop_source stop;
    EXPECT_FALSE(backend->start(operation(4, stop.get_token()), "unused"));
    stop.request_stop();
    EXPECT_FALSE(backend->poll(false));
    EXPECT_FALSE(backend->poll(false));
    EXPECT_EQ(api->cancels, 1);
    EXPECT_EQ(api->handles.size(), 1);
    EXPECT_EQ(api->owned.size(), 1);
    api->acknowledge(ERROR_OPERATION_ABORTED, 0);
    const auto done = backend->poll(false);
    ASSERT_TRUE(done);
    expect_cancelled(require_result(done));
    EXPECT_EQ(backend->statistics().cancellation_requests, 1);
    EXPECT_TRUE(api->owned.empty());
    EXPECT_TRUE(api->handles.empty());
}

/// @brief Both a normal completion race and an unexpected cancel error still require a packet.
class cancel_status_test : public iocp_test, public ::testing::WithParamInterface<DWORD>
{
};

/// @brief Cancellation has public-result precedence even when the native transfer succeeds.
TEST_P(cancel_status_test, failed_cancel_waits_for_successful_completion)
{
    api->cancel_status = GetParam();
    std::stop_source stop;
    EXPECT_FALSE(backend->start(operation(4, stop.get_token()), "unused"));
    stop.request_stop();
    EXPECT_FALSE(backend->poll(false));
    EXPECT_FALSE(api->handles.empty());
    api->acknowledge();
    const auto done = backend->poll(false);
    ASSERT_TRUE(done);
    expect_cancelled(require_result(done));
    EXPECT_EQ(backend->statistics().cancellation_races, GetParam() == ERROR_NOT_FOUND ? 1 : 0);
    EXPECT_EQ(backend->statistics().cancellation_errors, GetParam() == ERROR_NOT_FOUND ? 0 : 1);
}

INSTANTIATE_TEST_SUITE_P(native_errors, cancel_status_test,
                         ::testing::Values(ERROR_NOT_FOUND, ERROR_ACCESS_DENIED));

/// @brief Already-cancelled requests never open files or become kernel-owned.
TEST_F(iocp_test, cancelled_admission_has_no_native_side_effects)
{
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW(backend->start(operation(4, stop.get_token()), "unused"), jobs::job_cancelled);
    EXPECT_EQ(api->reads, 0);
    EXPECT_TRUE(api->handles.empty());
}
} // namespace atlas::streaming::tests
