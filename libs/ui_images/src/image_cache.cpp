/** @file @brief Nonblocking owner state transitions over streamer ready-job image requests. */
#include "cache_state.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui_images
{
namespace
{
struct owner_guard
{
    bool& busy; ///< Borrowed non-reentrant owner flag.

    explicit owner_guard(bool& value) : busy(value)
    {
        busy = true;
    } ///< Enter checked scope.

    ~owner_guard()
    {
        busy = false;
    } ///< Reset on every exit.
}; ///< No diagnostic callbacks can mutate an active cache operation.

/// @brief Exhausted identities never wrap or reuse released keys.
std::uint64_t next_key()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("UI image identities exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1));

    return value;
}
} // namespace

/// @brief Reserve metadata before any callback can complete on a worker.
image_cache::impl::impl(streaming::asset_streamer& source,
                        std::shared_ptr<const renderer_content::image_decoder> codec,
                        image_cache_limits policy,
                        std::shared_ptr<std::pmr::memory_resource> resource,
                        diagnostics::services services)
    : streamer(source), decoder(std::move(codec)), limits(policy), memory(std::move(resource)),
      destinations(std::move(services)), budget(std::make_shared<detail::decoded_budget>()),
      entries(0, memory.get())
{
    budget->limit = limits.max_decoded_bytes;
    entries.reserve(limits.max_images);
}

/// @brief Check thread identity before observing mutable owner state.
void image_cache::impl::check() const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy)
    {
        throw std::logic_error("UI image cache requires its non-reentrant owner");
    }
}

/// @brief Keys are process-unique; a missing key is stale or foreign to this cache.
detail::cache_entry& image_cache::impl::require(std::uint64_t key)
{
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [key](const auto& entry)
                                    {
                                        return entry.value.key == key;
                                    });
    if (found == entries.end())
    {
        throw std::invalid_argument("unknown UI image key");
    }

    return *found;
}

/// @brief Decoder/file policy is injected; no storage path is interpreted here.
image_cache::image_cache(streaming::asset_streamer& streamer,
                         std::shared_ptr<const renderer_content::image_decoder> decoder,
                         image_cache_limits limits,
                         std::shared_ptr<std::pmr::memory_resource> memory,
                         diagnostics::services destinations)
{
    jobs::require_coordinator_thread();
    if (!decoder || limits.max_images == 0 || limits.max_images > 4096 ||
        limits.max_decoded_bytes == 0)
    {
        throw std::invalid_argument("invalid UI image cache configuration");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(streamer, std::move(decoder), limits, std::move(memory),
                                   std::move(destinations));
}

/// @brief Accepted callbacks own decoder/ledger state and never borrow the destroyed facade.
image_cache::~image_cache()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
    for (auto& entry : impl_->entries)
    {
        if (entry.request)
        {
            entry.request->cancel();
        }
    }
}

/// @brief Worst-case decoded capacity is reserved before storage or ready-job admission.
std::uint64_t image_cache::request(streaming::read_request source,
                                   renderer_content::image_limits limits)
{
    impl_->check();
    const owner_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.images.request", "ui.cpu"}};
    limits.validate();
    if (impl_->entries.size() == impl_->limits.max_images)
    {
        throw std::overflow_error("UI image entry budget exhausted");
    }

    const auto key = next_key();
    auto lease = std::make_shared<detail::decoded_lease>(impl_->budget, limits.max_decoded_bytes);
    auto decoder = std::make_shared<detail::leased_decoder>(impl_->decoder, std::move(lease));
    renderer_content::content_loader loader{impl_->streamer, std::move(decoder), impl_->memory,
                                            impl_->destinations};
    auto request = loader.read_image(std::move(source), limits);
    impl_->entries.push_back({{key, image_status::pending, {}, {}}, std::move(request)});
    return key;
}

/// @brief Only ready futures are consumed; no worker or coordinator waits for storage here.
void image_cache::poll()
{
    impl_->check();
    const owner_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.images.poll", "ui.cpu"}};
    for (auto& entry : impl_->entries)
    {
        if (!entry.request || !entry.request->ready())
        {
            continue;
        }

        try
        {
            entry.value.pixels = entry.request->get();
            entry.value.status = image_status::decoded;
        }
        catch (const jobs::job_cancelled&)
        {
            entry.value.status = image_status::cancelled;
        }
        catch (...)
        {
            entry.value.status = image_status::failed;
            entry.value.error = std::current_exception();
            diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "ui.images",
                               "image preparation failed; placeholder retained");
        }

        entry.request.reset();
    }
}

/// @brief Cancellation wins even if decoding finished before the coordinator consumed it.
void image_cache::cancel(std::uint64_t key)
{
    impl_->check();
    const owner_guard guard{impl_->busy};
    auto& entry = impl_->require(key);
    if (entry.request)
    {
        entry.request->cancel();
    }

    entry.request.reset();
    entry.value.pixels.reset();
    entry.value.error = {};
    entry.value.status = image_status::cancelled;
}

/// @brief Removing one identity never makes an older frame's decoded bytes uncharged.
void image_cache::release(std::uint64_t key)
{
    cancel(key);
    const owner_guard guard{impl_->busy};
    const auto found = std::find_if(impl_->entries.begin(), impl_->entries.end(),
                                    [key](const auto& entry)
                                    {
                                        return entry.value.key == key;
                                    });
    impl_->entries.erase(found);
}

/// @brief Shared pixel aliases retain actual storage without copying decoded buffers.
image_frame image_cache::snapshot() const
{
    impl_->check();
    const owner_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.images.snapshot", "ui.cpu"}};
    auto data = std::make_shared<detail::image_frame_data>(impl_->memory);
    data->values.reserve(impl_->entries.size());
    for (const auto& entry : impl_->entries)
    {
        data->values.push_back(entry.value);
    }

    image_frame result;
    result.data_ = std::move(data);
    return result;
}

/// @brief Outstanding callback/frame leases are included even after their entry was removed.
image_cache_statistics image_cache::statistics() const
{
    impl_->check();
    return {impl_->entries.size(), impl_->budget->current.load(), impl_->budget->peak.load()};
}

/// @brief Default or moved-from frames expose an empty range.
std::span<const image_entry> image_frame::entries() const noexcept
{
    return data_ ? std::span<const image_entry>{data_->values} : std::span<const image_entry>{};
}

/// @brief Keys remain valid only within this immutable frame's retained storage.
const image_entry* image_frame::find(std::uint64_t key) const noexcept
{
    const auto values = entries();
    const auto found = std::lower_bound(values.begin(), values.end(), key,
                                        [](const auto& entry, std::uint64_t id)
                                        {
                                            return entry.key < id;
                                        });
    return found != values.end() && found->key == key ? &*found : nullptr;
}

/// @brief Placeholder sizing never requires successful acquisition or pixel access.
ui::layout_extent intrinsic_size(const image_entry& image, ui::layout_extent placeholder)
{
    if (!std::isfinite(placeholder.width) || !std::isfinite(placeholder.height) ||
        placeholder.width < 0 || placeholder.height < 0)
    {
        throw std::invalid_argument("invalid UI image placeholder size");
    }

    return image.status == image_status::decoded && image.pixels
               ? ui::layout_extent{static_cast<float>(image.pixels->width),
                                   static_cast<float>(image.pixels->height)}
               : placeholder;
}
} // namespace atlas::ui_images
