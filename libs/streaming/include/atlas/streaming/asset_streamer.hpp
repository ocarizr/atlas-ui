#pragma once

#include <atlas/jobs/job_system.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace atlas::streaming
{
using asset_bytes = std::vector<std::uint8_t>;
struct memory_asset
{
    std::shared_ptr<const asset_bytes> bytes;
};
struct loose_file_asset
{
    std::filesystem::path path;
};
using asset_source = std::variant<memory_asset, loose_file_asset>;
using asset_catalog = std::unordered_map<std::string, asset_source>;
struct read_request
{
    std::string asset_id;
    std::uint64_t offset;
    std::size_t size;
};
struct streamer_limits
{
    std::size_t max_read_bytes{std::size_t{4} * 1024 * 1024};
    std::size_t max_pending_requests{32};
};
using read_result = std::variant<std::shared_ptr<const asset_bytes>, std::exception_ptr>;
using read_completion = std::function<void(read_result)>;

// Callbacks run on CPU jobs and must not block. Only the creating coordinator
// may call shutdown/destruct; keep the job_system alive until shutdown finishes.
// Stop external callers before destroying the streamer.
class asset_streamer
{
  public:
    asset_streamer(jobs::job_system& jobs, asset_catalog catalog, streamer_limits limits = {});
    ~asset_streamer();
    asset_streamer(const asset_streamer&) = delete;
    asset_streamer& operator=(const asset_streamer&) = delete;
    asset_streamer(asset_streamer&&) = delete;
    asset_streamer& operator=(asset_streamer&&) = delete;

    // The future observes callback completion/errors; wait only on coordinators.
    // Accepted source/read failures reach the callback as exception_ptr.
    std::future<void> read(read_request request, read_completion completion);
    void shutdown();

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::streaming
