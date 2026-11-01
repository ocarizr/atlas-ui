/** @file
 * @brief SDK-free serial reference backend preserving the asynchronous pump contract.
 */
#include "file_io.hpp"
#include "asset_reader.hpp"
#include "streamer_state.hpp"

namespace atlas::streaming::detail
{
/// @brief Reference implementation: only its I/O owner may block on a file operation.
class standard_file_io final : public file_io
{
  public:
    /// @brief Produce a ready exact-range result through the standard-library reader.
    std::optional<read_result> start(const std::shared_ptr<read_operation>& operation,
                                     const std::filesystem::path& path) override
    {
        return read_file_asset(loose_file_asset{path}, operation->request, operation->reservation);
    }

    /// @brief Wait for a persistent wake flag, preventing queue/shutdown notification loss.
    std::optional<file_completion> poll(bool block) noexcept override
    {
        std::unique_lock lock{mutex_};
        if (block)
        {
            changed_.wait(lock,
                          [this]
                          {
                              return notified_;
                          });
        }

        notified_ = false;
        return std::nullopt;
    }

    /// @brief Notify the I/O owner from producers, stop callbacks or shutdown.
    void wake() noexcept override
    {
        const std::lock_guard lock{mutex_};
        notified_ = true;
        changed_.notify_one();
    }

  private:
    /// @brief Protect the persistent wake flag across arbitrary producer threads.
    std::mutex mutex_;
    /// @brief Storage-owner notification, unrelated to the CPU scheduler.
    std::condition_variable changed_;
    /// @brief A wake received before poll must not be lost.
    bool notified_{false};
};

/// @brief Construct the SDK-free implementation selected by CMake.
std::unique_ptr<file_io> make_file_io()
{
    return std::make_unique<standard_file_io>();
}
} // namespace atlas::streaming::detail
