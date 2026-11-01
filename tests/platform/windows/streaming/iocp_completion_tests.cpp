/** @file @brief Packet ownership, chunking and fatal port recovery invariants. */
#include "controlled_iocp.hpp"

namespace atlas::streaming::tests
{
/// @brief Immediate ReadFile success cannot release resources before its completion packet.
TEST_F(iocp_test, synchronous_success_still_waits_for_packet)
{
    api->synchronous = true;
    const auto work = operation();
    EXPECT_FALSE(backend->start(work, "unused"));
    EXPECT_FALSE(backend->poll(false));
    EXPECT_EQ(api->handles.size(), 1);
    api->acknowledge();
    auto done = require_result(backend->poll(false));

    EXPECT_EQ(done.operation, work);
    EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(done.result),
              (asset_bytes{'X', 'X', 'X', 'X'}));
    EXPECT_TRUE(api->handles.empty());
    EXPECT_FALSE(backend->poll(false));
}

/// @brief Short transfers never publish partial output as a successful exact read.
TEST_F(iocp_test, rejects_short_completion)
{
    EXPECT_FALSE(backend->start(operation(), "unused"));
    api->acknowledge(ERROR_SUCCESS, 2);
    const auto done = require_result(backend->poll(false));

    EXPECT_TRUE(std::holds_alternative<std::exception_ptr>(done.result));
}

/// @brief Large offsets and chunk reuse remain correct across the 32-bit offset boundary.
TEST_F(iocp_test, chunks_cross_high_offset_boundary)
{
    backend = detail::make_iocp_backend(api, 2);
    api->length = (std::uint64_t{1} << 32) + 4;
    auto work = operation();
    work->request.offset = (std::uint64_t{1} << 32) - 1;
    EXPECT_FALSE(backend->start(work, "unused"));
    EXPECT_EQ(api->offset, work->request.offset);
    api->acknowledge();
    EXPECT_FALSE(backend->poll(false));
    EXPECT_EQ(api->offset, work->request.offset + 2);
    EXPECT_EQ(api->reads, 2);
    api->acknowledge();
    ASSERT_TRUE(backend->poll(false));
}

/// @brief A zero-byte EOF range needs no native transfer or completion packet.
TEST_F(iocp_test, empty_eof_read_completes_without_submission)
{
    auto work = operation(0);
    work->request.offset = api->length;
    auto ready = require_result(backend->start(work, "unused"));

    EXPECT_TRUE(std::get<std::shared_ptr<const asset_bytes>>(ready)->empty());
    EXPECT_EQ(api->reads, 0);
    EXPECT_TRUE(api->handles.empty());
}

/// @brief A broken port cannot strand requests or free OS-owned payloads prematurely.
TEST_F(iocp_test, fatal_port_failure_drains_all_requests_before_retiring)
{
    EXPECT_FALSE(backend->start(operation(), "unused"));
    EXPECT_FALSE(backend->start(operation(), "unused"));
    api->packets.push_back({nullptr, 0, ERROR_INVALID_HANDLE});
    auto first = backend->poll(false);
    ASSERT_TRUE(first);
    EXPECT_TRUE(backend->failure());
    EXPECT_EQ(api->finishes, 2);
    EXPECT_TRUE(api->owned.empty());
    auto second = backend->poll(false);
    ASSERT_TRUE(second);
    EXPECT_TRUE(
        std::holds_alternative<std::exception_ptr>(require_result(std::move(second)).result));
    EXPECT_THROW(backend->start(operation(), "unused"), std::system_error);
}

/// @brief Destruction performs an emergency cancel-and-wait before closing files and port.
TEST_F(iocp_test, destruction_drains_unacknowledged_kernel_work)
{
    EXPECT_FALSE(backend->start(operation(), "unused"));
    backend.reset();
    EXPECT_EQ(api->cancels, 1);
    EXPECT_EQ(api->finishes, 1);
    EXPECT_TRUE(api->handles.empty());
}

/// @brief Every failure before OS admission releases handles and allows subsequent requests.
TEST_F(iocp_test, submission_failures_leave_backend_reusable)
{
    api->fail_open = true;
    EXPECT_THROW(backend->start(operation(), "unused"), std::system_error);
    api->fail_open = false;
    api->fail_size = true;
    EXPECT_THROW(backend->start(operation(), "unused"), std::runtime_error);
    api->fail_size = false;
    api->fail_associate = true;
    EXPECT_THROW(backend->start(operation(), "unused"), std::runtime_error);
    api->fail_associate = false;
    api->read_error = ERROR_ACCESS_DENIED;
    EXPECT_THROW(backend->start(operation(), "unused"), std::system_error);
    EXPECT_TRUE(api->handles.empty());
    EXPECT_TRUE(api->owned.empty());
    api->read_error = ERROR_SUCCESS;
    EXPECT_FALSE(backend->start(operation(), "unused"));
    api->acknowledge();
    ASSERT_TRUE(backend->poll(false));
}
} // namespace atlas::streaming::tests
