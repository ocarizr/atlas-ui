/** @file @brief Deterministic native-call model that detects premature request destruction. */
#pragma once
#include "iocp_api.hpp"
#include "streamer_state.hpp"
#include <array>
#include <deque>
#include <gtest/gtest.h>
#include <system_error>
#include <unordered_set>

namespace atlas::streaming::tests
{
/// @brief Model kernel ownership independently of completion speed or physical disk behavior.
class controlled_iocp final : public detail::iocp_api
{
  public:
    /// @brief Return a distinguished fake port, never passed to actual Win32 calls.
    HANDLE create_port() override
    {
        return &port_identity;
    }

    /// @brief Allocate a distinct logical handle or inject an open failure.
    HANDLE open_file(const std::filesystem::path&) override
    {
        if (fail_open)
        {
            throw std::system_error{std::make_error_code(std::errc::permission_denied)};
        }

        const auto handle = &identities.at(next_handle++);
        handles.insert(handle);
        return handle;
    }

    /// @brief Return a configurable size or inject metadata failure.
    std::uint64_t file_size(HANDLE) override
    {
        if (fail_size)
        {
            throw std::runtime_error{"size failure"};
        }

        return length;
    }

    /// @brief Inject association failure before any request becomes OS-owned.
    void associate(HANDLE, HANDLE) override
    {
        if (fail_associate)
        {
            throw std::runtime_error{"association failure"};
        }
    }

    /// @brief Record a kernel-owned chunk; synchronous success still needs its queued packet.
    DWORD read(HANDLE file, std::span<std::uint8_t> bytes, OVERLAPPED& native) noexcept override
    {
        ++reads;
        offset = (static_cast<std::uint64_t>(native.OffsetHigh) << 32) | native.Offset;
        if (read_error != ERROR_SUCCESS)
        {
            return read_error;
        }

        try
        {
            owned.emplace(&native, file);
        }
        catch (...)
        {
            return ERROR_NOT_ENOUGH_MEMORY;
        }

        current = &native;
        current_file = file;
        transfer = static_cast<DWORD>(bytes.size());
        std::fill(bytes.begin(), bytes.end(), static_cast<std::uint8_t>('X'));
        return synchronous ? ERROR_SUCCESS : ERROR_IO_PENDING;
    }

    /// @brief Record cancellation without acknowledging it; the test chooses the terminal packet.
    DWORD cancel(HANDLE, OVERLAPPED&) noexcept override
    {
        ++cancels;
        return cancel_status;
    }

    /// @brief Release kernel ownership only when the completion is actually dequeued.
    detail::completion_packet wait(HANDLE, bool) noexcept override
    {
        if (packets.empty())
        {
            return {nullptr, 0, WAIT_TIMEOUT};
        }

        auto packet = packets.front();
        packets.pop_front();
        if (packet.overlapped)
        {
            owned.erase(packet.overlapped);
        }

        return packet;
    }

    /// @brief Emergency drain acknowledges ownership without relying on a functioning port.
    void finish(HANDLE, OVERLAPPED& native) noexcept override
    {
        ++finishes;
        owned.erase(&native);
    }

    /// @brief Polls are explicitly driven by the tests, so wake needs no scheduling behavior.
    void wake(HANDLE) noexcept override
    {
    }

    /// @brief Detect native handles released before the kernel acknowledgment.
    void close(HANDLE handle) noexcept override
    {
        if (std::any_of(owned.begin(), owned.end(),
                        [handle](const auto& entry)
                        {
                            return entry.second == handle;
                        }))
        {
            unsafe_close = true;
        }

        handles.erase(handle);
    }

    /// @brief Queue one chosen completion for the latest submitted chunk.
    void acknowledge(DWORD error = ERROR_SUCCESS, std::optional<DWORD> bytes = {})
    {
        packets.push_back({current, bytes.value_or(transfer), error});
    }

    /// @brief Fake native file identities still open.
    std::unordered_set<HANDLE> handles;
    /// @brief Requests whose destination is still considered kernel-owned.
    std::unordered_map<OVERLAPPED*, HANDLE> owned;
    /// @brief Controlled completion queue.
    std::deque<detail::completion_packet> packets;
    /// @brief Current transfer identity retained until a packet or emergency wait.
    OVERLAPPED* current{};
    /// @brief Current file identity.
    HANDLE current_file{};
    /// @brief Fake identity generator.
    std::size_t next_handle{0};
    /// @brief Stable opaque file identities backed by ordinary object addresses.
    std::array<std::uint8_t, 32> identities{};
    /// @brief Distinct port identity kept separate from the file handle slots.
    std::uint8_t port_identity{};
    /// @brief Source size; large values test high offset bits without allocating huge buffers.
    std::uint64_t length{16};
    /// @brief Last submitted absolute offset.
    std::uint64_t offset{};
    /// @brief Last requested chunk size.
    DWORD transfer{};
    /// @brief Immediate read submission failure, if configured.
    DWORD read_error{ERROR_SUCCESS};
    /// @brief Controlled cancellation return status.
    DWORD cancel_status{ERROR_SUCCESS};
    /// @brief Native operation observations.
    int reads{0}, cancels{0}, finishes{0};
    /// @brief ReadFile may report immediate success but its packet remains outstanding.
    bool synchronous{false};
    /// @brief Failure switches applied before native request admission.
    bool fail_open{false}, fail_size{false}, fail_associate{false};
    /// @brief Forbidden close observed while a request was still owned by the fake kernel.
    bool unsafe_close{false};
};

/// @brief Shared operation builders and safe teardown for private IOCP contract tests.
class iocp_test : public ::testing::Test
{
  protected:
    /// @brief Require a terminal result with a branch visible to static optional-access analysis.
    template <class Value> static Value require_result(std::optional<Value> result)
    {
        if (!result.has_value())
        {
            throw std::runtime_error{"Expected a terminal I/O result"};
        }

        return std::move(*result);
    }

    /// @brief Stop/drain all fake native activity before checking handle cleanup.
    void TearDown() override
    {
        backend.reset();
        EXPECT_TRUE(api->handles.empty());
        EXPECT_TRUE(api->owned.empty());
        EXPECT_FALSE(api->unsafe_close);
    }

    /// @brief Build an admitted logical read with a real bounded-buffer reservation.
    std::shared_ptr<detail::read_operation> operation(std::size_t size = 4,
                                                      std::stop_token cancellation = {})
    {
        auto work = std::make_shared<detail::read_operation>();
        work->request = {"test", 0, size};
        work->options.cancellation = std::move(cancellation);
        work->reservation = detail::reserve_buffer(memory, size, 1024);
        return work;
    }

    /// @brief Check the exact cancellation diagnostic delivered by a terminal read.
    void expect_cancelled(const detail::file_completion& completion)
    {
        ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(completion.result));
        EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(completion.result)),
                     jobs::job_cancelled);
    }

    /// @brief Native call observations outlive backend destruction.
    std::shared_ptr<controlled_iocp> api{std::make_shared<controlled_iocp>()};
    /// @brief Actual output allocator used by the private backend.
    std::shared_ptr<detail::buffer_memory> memory{std::make_shared<detail::buffer_memory>(nullptr)};
    /// @brief IOCP state exercised without actual disk timing.
    std::unique_ptr<detail::file_io> backend{detail::make_iocp_backend(api)};
};
} // namespace atlas::streaming::tests
