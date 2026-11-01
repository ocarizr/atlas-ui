/**
 * @file
 * @brief Exclusive storage acquisition with persistent I/O and job-based ready delivery.
 */

#pragma once

#include <atlas/jobs/job_system.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <memory_resource>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace atlas::streaming
{
/// @brief Owned contiguous bytes delivered immutably by the streamer.
using asset_bytes = std::pmr::vector<std::uint8_t>;

/// @brief Pinned immutable backing bytes; external aliases must not mutate the same storage.
struct memory_asset
{
    /// @brief Shared immutable source bytes; external aliases must not mutate them.
    std::shared_ptr<const asset_bytes> bytes;
};

/// @brief Authorized physical source known only to the storage boundary and composition root.
struct loose_file_asset
{
    /// @brief Authorized physical source path retained only at the storage boundary.
    std::filesystem::path path;
};

/// @brief Registered memory or authorized loose-file source, hidden from media consumers.
using asset_source = std::variant<memory_asset, loose_file_asset>;
/// @brief Immutable logical asset-ID to registered source mapping.
using asset_catalog = std::unordered_map<std::string, asset_source>;

/// @brief Exact asset-relative byte range; oversized or overflowing reads are rejected.
struct read_request
{
    /// @brief Logical catalog key; consumers do not receive a physical file path.
    std::string asset_id;
    /// @brief Asset-relative byte offset validated against source size and integer overflow.
    std::uint64_t offset;
    /// @brief Exact byte count requested; partial data is not published.
    std::size_t size;
};

/// @brief Per-read size and accepted-operation limits, including completion callbacks in flight.
struct streamer_limits
{
    /// @brief Largest accepted exact read; larger assets must be requested in ranges.
    std::size_t max_read_bytes{std::size_t{4} * 1024 * 1024};
    /// @brief Accepted read-operation limit including completion callbacks in flight.
    std::size_t max_pending_requests{32};
    /// @brief Maximum requested output bytes reserved or retained by consumers, excluding source
    /// data and metadata.
    std::size_t max_resident_bytes{std::size_t{64} * 1024 * 1024};
    /// @brief Opt-in minimum copy size for disjoint children on an available pool; zero disables
    /// uncalibrated splitting.
    std::size_t parallel_copy_threshold{0};
    /// @brief Positive cache-line-multiple target chunk size; runtime also caps chunk count by
    /// worker count.
    std::size_t copy_grain_bytes{std::size_t{256} * 1024};
};

/// @brief CPU scheduling priority and cooperative read cancellation; neither preempts disk I/O.
struct read_options
{
    /// @brief Weighted scheduler class used by ready acquisition/delivery work.
    jobs::job_priority priority{jobs::job_priority::background};
    /// @brief Caller-owned cancellation signal checked before acquisition, between chunks and
    /// before delivery.
    std::stop_token cancellation{};
};

/// @brief Observational counters; byte reservations include outputs retained after delivery.
struct streamer_statistics
{
    /// @brief Accepted operations whose completion delivery has not retired.
    std::size_t pending;
    /// @brief Bytes reserved for reads or held by live output buffers.
    std::size_t resident_bytes;
    /// @brief Highest observed simultaneously reserved output byte count.
    std::size_t peak_resident_bytes;
    /// @brief Reads that used one CPU copy without child dispatch.
    std::size_t serial_copies;
    /// @brief Reads partitioned into disjoint attached copy jobs.
    std::size_t parallel_copies;
    /// @brief Retired completion deliveries, including errors and cancellation.
    std::size_t completed;
    /// @brief Retired acquisition/cancellation or callback failures.
    std::size_t failed;
};

/// @brief Owned immutable exact bytes or the original acquisition failure.
using read_result = std::variant<std::shared_ptr<const asset_bytes>, std::exception_ptr>;
/// @brief Ready-result callback executed as CPU work; it must not block on another read.
using read_completion = std::function<void(read_result)>;

/// Callbacks run on CPU jobs and must not block. Only the creating coordinator
/// may call shutdown/destruct; keep the job_system alive until shutdown finishes.
/// Stop external callers before destroying the streamer.
class asset_streamer
{
  public:
    /// @brief Own an immutable catalog and persistent I/O thread while borrowing a pool that must
    /// outlive draining.
    ///
    /// @param jobs Application-owned pool that must outlive accepted streamer work and draining.
    /// @param catalog Immutable logical source registrations; memory aliases must remain immutable.
    /// @param limits Positive per-read and in-flight request budgets.
    /// @param buffers Optional shared thread-safe payload allocator; retained outputs keep it
    /// alive. Null selects new/delete.
    asset_streamer(jobs::job_system& jobs, asset_catalog catalog, streamer_limits limits = {},
                   std::shared_ptr<std::pmr::memory_resource> buffers = {});
    /// @brief Drain accepted work and release persistent resources; callers must obey the enclosing
    /// coordinator/lifetime contract.
    ~asset_streamer();
    /// @brief Own an immutable catalog and persistent I/O thread while borrowing a pool that must
    /// outlive draining.
    asset_streamer(const asset_streamer&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    asset_streamer& operator=(const asset_streamer&) = delete;
    /// @brief Own an immutable catalog and persistent I/O thread while borrowing a pool that must
    /// outlive draining.
    asset_streamer(asset_streamer&&) = delete;
    /// @brief Disable move construction/assignment to preserve unique lifecycle ownership.
    asset_streamer& operator=(asset_streamer&&) = delete;

    /// The future observes callback completion/errors; wait only on coordinators.
    /// Accepted source/read failures reach the callback as exception_ptr.
    std::future<void> read(read_request request, read_completion completion,
                           read_options options = {});
    /// @brief Return thread-safe observational admission, allocation and scheduling counters.
    [[nodiscard]] streamer_statistics statistics() const;
    /// @brief On the creating coordinator, reject new reads, drain accepted callbacks and join I/O
    /// before destroying storage.
    void shutdown();

  private:
    /// @brief Private implementation state; callers interact through the enclosing public API.
    struct impl;
    /// @brief Owned private implementation storage; destruction follows the enclosing lifecycle
    /// contract.
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::streaming
