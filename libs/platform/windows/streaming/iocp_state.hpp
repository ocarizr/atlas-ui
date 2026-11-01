/** @file @brief Private request ownership shared by IOCP submission and completion processing. */
#pragma once
#include "iocp_api.hpp"
#include "streamer_state.hpp"
#include <unordered_map>

namespace atlas::streaming::detail
{
/// @brief Capture an allocation-safe terminal error without escaping a noexcept poll.
std::exception_ptr io_error(DWORD error) noexcept;

/// @brief Stable native request; never moved while the kernel can reference its OVERLAPPED.
struct iocp_read
{
    /// @brief Open last so a throwing constructor cannot leak a native handle.
    iocp_read(iocp_api& calls, std::shared_ptr<read_operation> operation,
              const std::filesystem::path& path);
    /// @brief Close only after the owner consumes completion or explicitly drains the kernel.
    ~iocp_read();
    /// @brief Native request identity cannot be copied while referenced by the kernel.
    iocp_read(const iocp_read&) = delete;
    /// @brief Native resource ownership cannot be reassigned.
    iocp_read& operator=(const iocp_read&) = delete;
    /// @brief Stable completion identity, reset only between acknowledged chunks.
    OVERLAPPED native{};
    /// @brief Adapter outlives every request.
    iocp_api& api;
    /// @brief Accepted logical read and its quota reservation.
    std::shared_ptr<read_operation> operation;
    /// @brief Mutable destination kept private until the exact range is complete.
    std::shared_ptr<asset_bytes> output;
    /// @brief Count of acknowledged bytes preceding the current chunk.
    std::size_t progress{0};
    /// @brief Requested bytes in the outstanding chunk.
    std::size_t transfer{0};
    /// @brief Whether cancellation has already been requested for this operation.
    bool cancellation_sent{false};
    /// @brief Whether a submitted chunk still needs kernel acknowledgment.
    bool pending{false};
    /// @brief Handle for this opened loose-file identity.
    HANDLE file;
};

/// @brief Single-owner IOCP state; only wake and atomic statistics are cross-thread operations.
class iocp_backend final : public file_io
{
  public:
    /// @brief Validate chunk size and create the port before starting the pump.
    iocp_backend(std::shared_ptr<iocp_api> api, std::size_t transfer_limit);
    /// @brief Emergency drain precedes request and port destruction.
    ~iocp_backend() override;
    /// @brief Validate/open/allocate before submitting any OS-owned request.
    std::optional<read_result> start(const std::shared_ptr<read_operation>& operation,
                                     const std::filesystem::path& path) override;
    /// @brief Advance chunks or return one terminal request after OS acknowledgment.
    std::optional<file_completion> poll(bool block) noexcept override;

    /// @brief Wake the owner from admission, shutdown or stop callbacks.
    void wake() noexcept override
    {
        api_->wake(port_);
    }

    /// @brief Expose a terminal port failure to the common admission pump.
    std::exception_ptr failure() const noexcept override
    {
        return failure_;
    }

    /// @brief Snapshot cross-thread cancellation observations.
    file_io_statistics statistics() const noexcept override;

  private:
    /// @brief Submit the next exact chunk without allocating after OS admission.
    DWORD issue(iocp_read& request) noexcept;
    /// @brief Send cancellation once for a stopped request without freeing its storage.
    void cancel(iocp_read& request) noexcept;
    /// @brief Remove an acknowledged request and return its immutable result or error.
    file_completion complete(OVERLAPPED* identity, std::exception_ptr error) noexcept;
    /// @brief Cancel and drain every kernel-owned chunk before releasing native storage.
    void drain() noexcept;
    /// @brief Native adapter retained by the port and all files.
    std::shared_ptr<iocp_api> api_;
    /// @brief Port closed after every file request.
    HANDLE port_{};
    /// @brief Maximum bytes per native transfer, at most DWORD_MAX.
    std::size_t transfer_limit_;
    /// @brief Stable heap requests keyed by their OVERLAPPED addresses.
    std::unordered_map<OVERLAPPED*, std::unique_ptr<iocp_read>> active_;
    /// @brief Fatal port error; queued/new reads fail rather than becoming stranded.
    std::exception_ptr failure_;
    /// @brief Count of actual CancelIoEx attempts.
    std::atomic<std::size_t> cancellations_{0};
    /// @brief Attempts for which completion had already won the native race.
    std::atomic<std::size_t> races_{0};
    /// @brief Unexpected native cancellation failures, still followed by draining.
    std::atomic<std::size_t> errors_{0};
};
} // namespace atlas::streaming::detail
