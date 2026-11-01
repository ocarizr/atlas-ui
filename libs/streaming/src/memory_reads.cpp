/** @file
 * @brief Size/load-aware memory copies with explicit child ownership and no worker waits.
 */
#include "asset_reader.hpp"
#include "streamer_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::streaming::detail
{
namespace
{
/// @brief Shared copy ownership and an outstanding-work counter including the submitting parent.
struct memory_copy
{
    /// @brief Shared completion/admission state.
    std::shared_ptr<streamer_state> state;
    /// @brief Accepted read and caller cancellation signal.
    std::shared_ptr<read_operation> operation;
    /// @brief Immutable source pinned until all copy bodies finish.
    std::shared_ptr<const asset_bytes> source;
    /// @brief Distinct aligned output retained until publication.
    std::shared_ptr<asset_bytes> output;
    /// @brief Parent submission claim plus admitted copy bodies.
    std::atomic<std::size_t> remaining{1};
    /// @brief Synchronizes competing original copy/admission errors.
    std::mutex mutex;
    /// @brief First failure; no partial buffer is published.
    std::exception_ptr failure;

    /// @brief Record an error without overwriting an earlier cause.
    void fail(const std::exception_ptr& error)
    {
        const std::lock_guard lock{mutex};
        if (!failure)
        {
            failure = error;
        }
    }

    /// @brief Last body/submission claim publishes complete bytes or the original failure exactly
    /// once.
    void finish()
    {
        if (remaining.fetch_sub(1) != 1)
        {
            return;
        }

        if (failure)
        {
            output.reset();
            state->deliver(operation, failure);
        }
        else
        {
            state->deliver(operation, std::shared_ptr<const asset_bytes>{std::move(output)});
        }
    }
};

/// @brief One disjoint byte range plus retained copy ownership.
struct copy_chunk
{
    /// @brief Immutable input, owned output and completion counter.
    std::shared_ptr<memory_copy> copy;
    /// @brief Output-relative chunk start; source adds the requested asset offset.
    std::size_t offset;
    /// @brief Exact chunk length, bounded by both source and output.
    std::size_t size;
};

/// @brief Copy disjoint declared regions and always release the chunk's completion claim.
void copy_bytes(jobs::job_context& context, copy_chunk& chunk)
{
    try
    {
        if (chunk.copy->operation->options.cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto input = std::span<const std::uint8_t>{*chunk.copy->source}.subspan(
            static_cast<std::size_t>(chunk.copy->operation->request.offset) + chunk.offset,
            chunk.size);
        const auto output =
            std::span<std::uint8_t>{*chunk.copy->output}.subspan(chunk.offset, chunk.size);
        auto reading = context.access(jobs::read_access(input));
        auto writing = context.access(jobs::write_access(output));
        std::copy(input.begin(), input.end(), output.begin());
    }
    catch (...)
    {
        chunk.copy->fail(std::current_exception());
    }

    chunk.copy->finish();
}
} // namespace

/// @brief Use one copy for small/busy/single-worker cases and bounded children for substantial
/// ready work.
void streamer_state::read_memory(jobs::job_context& context,
                                 const std::shared_ptr<read_operation>& operation)
{
    try
    {
        if (operation->options.cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto found = catalog.find(operation->request.asset_id);
        if (found == catalog.end())
        {
            throw std::out_of_range{"Asset ID is not registered"};
        }

        const auto& source = std::get<memory_asset>(found->second).bytes;
        if (!source)
        {
            throw std::invalid_argument{"Memory asset has no backing bytes"};
        }

        validate_range(source->size(), operation->request);
        auto copy = std::make_shared<memory_copy>();
        copy->state = shared_from_this();
        copy->operation = operation;
        copy->source = source;
        copy->output = allocate_buffer(operation->reservation);
        const auto load = jobs.statistics();
        const auto size = operation->request.size;
        const auto target =
            size / load.workers + static_cast<std::size_t>(size % load.workers != 0);
        const auto aligned_target = (target + 63) / 64 * 64;
        const auto grain = std::max(limits.copy_grain_bytes, aligned_target);
        const bool parallel = limits.parallel_copy_threshold != 0 &&
                              size >= limits.parallel_copy_threshold && size > grain &&
                              load.workers > 1 && load.queued == 0 && load.running == 1;
        if (!parallel)
        {
            ++serial_copies;
            copy_chunk chunk{copy, 0, size};
            ++copy->remaining;
            copy_bytes(context, chunk);
        }
        else
        {
            ++parallel_copies;
            for (std::size_t offset = 0; offset < size;)
            {
                const auto count = std::min(grain, size - offset);
                ++copy->remaining;
                try
                {
                    context.spawn(jobs::job_label{"stream.copy", "streaming"},
                                  copy_chunk{copy, offset, count}, &copy_bytes);
                }
                catch (...)
                {
                    copy->fail(std::current_exception());
                    copy->finish();
                    break;
                }

                offset += count;
            }
        }

        copy->finish();
    }
    catch (...)
    {
        deliver(operation, std::current_exception());
    }
}
} // namespace atlas::streaming::detail
