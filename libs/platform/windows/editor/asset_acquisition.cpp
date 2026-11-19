/** @file @brief Bounded native I/O followed by ready-job hashing and typed content inspection. */
#include "asset_acquisition.hpp"
#include "asset_analysis.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <chrono>

namespace atlas::editor
{
/// @brief The SHA-256 digest detects changed sources at equal byte lengths.
std::string asset_source_hash(std::span<const std::uint8_t> bytes)
{
    std::array<UCHAR, 32> digest{};
    // The Windows one-shot hashing ABI accepts a mutable pointer for read-only input.
    const auto status = BCryptHash(
        BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size()));
    if (status < 0)
    {
        throw std::runtime_error("SHA-256 asset fingerprint failed");
    }

    constexpr std::string_view hex = "0123456789abcdef";
    std::string output;
    output.reserve(64);
    for (const auto value : digest)
    {
        output.push_back(hex[value >> 4]);
        output.push_back(hex[value & 15]);
    }

    return output;
}

namespace
{
/// @brief Probe native metadata without opening a content acquisition channel.
std::uint64_t source_size(const std::filesystem::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "Inspect editor asset source");
    }

    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        throw std::invalid_argument("Editor asset sources must be regular files");
    }

    return (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

/// @brief Future completion is the publication barrier for this mailbox.
struct prepared_source
{
    asset_payload payload; ///< No owner-thread objects or callbacks.
};

/// @brief Both the streamer and its result remain owned until cancellation has drained.
class source_read final : public asset_read
{
  public:
    /// @brief Inspect metadata on the coordinator, then leave I/O to the storage implementation.
    source_read(const std::filesystem::path& path, assets::asset_type type, jobs::job_system& jobs,
                const diagnostics::services& sinks)
    {
        const auto size = source_size(path);
        if (!size || size > std::uint64_t{16} * 1024 * 1024)
        {
            throw std::length_error("Editor imports accept nonempty sources up to 16 MiB");
        }

        streaming::streamer_limits limits;
        limits.max_read_bytes = std::size_t{16} * 1024 * 1024;
        reader_ = std::make_unique<streaming::asset_streamer>(
            jobs, streaming::asset_catalog{{"source", streaming::loose_file_asset{path}}}, limits,
            nullptr, sinks);
        future_ = reader_->read(
            {"source", 0, static_cast<std::size_t>(size)},
            [output = output_, type, token = stop_.get_token(),
             sinks](streaming::read_result result)
            {
                const diagnostics::operation_scope scope{
                    sinks, {"editor.assets.inspect", "editor.assets"}};
                if (const auto* error = std::get_if<std::exception_ptr>(&result))
                {
                    std::rethrow_exception(*error);
                }

                if (token.stop_requested())
                {
                    throw jobs::job_cancelled{};
                }

                auto bytes =
                    std::get<std::shared_ptr<const streaming::asset_bytes>>(std::move(result));
                output->payload = {bytes, inspect_asset(type, *bytes), asset_source_hash(*bytes)};
                if (token.stop_requested())
                {
                    throw jobs::job_cancelled{};
                }
            },
            {.priority = jobs::job_priority::background, .cancellation = stop_.get_token()});
    }

    ~source_read() override
    {
        stop_.request_stop();
        reader_->shutdown();
    } ///< Reader joins delivery before mailbox and cancellation state retire.

    bool ready() const override
    {
        return future_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    } ///< Never wait on the editor owner during polling.

    asset_payload take() override
    {
        future_.get();
        return std::move(output_->payload);
    } ///< Propagate ready-job failure without publishing partial metadata.

    void cancel() noexcept override
    {
        stop_.request_stop();
    } ///< Real I/O cancellation through the streamer.
  private:
    std::stop_source stop_; ///< Shared with I/O and CPU inspection.
    std::shared_ptr<prepared_source> output_ = std::make_shared<prepared_source>(); ///< Mailbox.
    std::unique_ptr<streaming::asset_streamer> reader_; ///< Persistent I/O for one accepted input.
    std::future<void> future_; ///< Release/acquire synchronization for prepared bytes.
};
} // namespace

/// @brief The returned owner contains all asynchronous state and no repository pointer.
std::unique_ptr<asset_read> acquire_editor_asset(const std::filesystem::path& path,
                                                 assets::asset_type type, jobs::job_system& jobs,
                                                 const diagnostics::services& sinks)
{
    return std::make_unique<source_read>(path, type, jobs, sinks);
}
} // namespace atlas::editor
