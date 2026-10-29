#pragma once

#include <atlas/jobs/JobSystem.hpp>

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
using AssetBytes = std::vector<std::uint8_t>;
struct MemoryAsset
{
    std::shared_ptr<const AssetBytes> bytes;
};
struct LooseFileAsset
{
    std::filesystem::path path;
};
using AssetSource = std::variant<MemoryAsset, LooseFileAsset>;
using AssetCatalog = std::unordered_map<std::string, AssetSource>;
struct ReadRequest
{
    std::string asset_id;
    std::uint64_t offset;
    std::size_t size;
};
struct StreamerLimits
{
    std::size_t max_read_bytes{std::size_t{4} * 1024 * 1024};
    std::size_t max_pending_requests{32};
};
using ReadResult = std::variant<std::shared_ptr<const AssetBytes>, std::exception_ptr>;
using ReadCompletion = std::function<void(ReadResult)>;

// Callbacks run on CPU jobs and must not block. Only the creating coordinator
// may call shutdown/destruct; keep the JobSystem alive until shutdown finishes.
// Stop external callers before destroying the streamer.
class AssetStreamer
{
  public:
    AssetStreamer(jobs::JobSystem& jobs, AssetCatalog catalog, StreamerLimits limits = {});
    ~AssetStreamer();
    AssetStreamer(const AssetStreamer&) = delete;
    AssetStreamer& operator=(const AssetStreamer&) = delete;
    AssetStreamer(AssetStreamer&&) = delete;
    AssetStreamer& operator=(AssetStreamer&&) = delete;

    // The future observes callback completion/errors; wait only on coordinators.
    // Accepted source/read failures reach the callback as exception_ptr.
    std::future<void> read(ReadRequest request, ReadCompletion completion);
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas::streaming
