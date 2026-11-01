/** @file
 * @brief Asynchronous storage interface shared by native and SDK-free implementations.
 */
#pragma once
#include <atlas/streaming/asset_streamer.hpp>
#include <optional>

namespace atlas::streaming::detail
{
/// @brief Accepted operation whose ownership survives submission and OS completion.
struct read_operation;

/// @brief One completed storage request, ready for CPU delivery.
struct file_completion
{
    /// @brief Accepted request retained until its callback is retired.
    std::shared_ptr<read_operation> operation;
    /// @brief Complete immutable bytes or the acquisition/cancellation error.
    read_result result;
};

/// @brief Atomic observations of native cancellation attempts and completion races.
struct file_io_statistics
{
    /// @brief Requests passed to native cancellation, not confirmed aborts.
    std::size_t cancellation_requests{0};
    /// @brief Cancellation attempts that found the operation already complete.
    std::size_t cancellation_races{0};
    /// @brief Other cancellation API failures; ownership still waits for OS completion.
    std::size_t cancellation_errors{0};
};

/// @brief Single-I/O-owner adapter; only wake and statistics may be called concurrently.
class file_io
{
  public:
    /// @brief Drain native ownership before releasing backend resources.
    virtual ~file_io() = default;
    /// @brief Start a read, return an immediate result or retain ownership until poll; throws only
    /// before OS admission.
    virtual std::optional<read_result> start(const std::shared_ptr<read_operation>& operation,
                                             const std::filesystem::path& path) = 0;
    /// @brief Return one completed request or a wake event; blocking is permitted only on the I/O
    /// owner.
    virtual std::optional<file_completion> poll(bool block) noexcept = 0;
    /// @brief Wake the I/O owner after enqueue, cancellation or shutdown; never invoke consumer
    /// code.
    virtual void wake() noexcept = 0;

    /// @brief Report a terminal backend failure after outstanding kernel ownership has been
    /// drained.
    virtual std::exception_ptr failure() const noexcept
    {
        return {};
    }

    /// @brief Copy counters without inspecting owner-thread native request containers.
    virtual file_io_statistics statistics() const noexcept
    {
        return {};
    }
};

/// @brief Construct exactly the backend selected by the platform build configuration.
std::unique_ptr<file_io> make_file_io();
} // namespace atlas::streaming::detail
