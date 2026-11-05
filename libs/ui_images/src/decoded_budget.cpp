/** @file @brief Reservation accounting follows accepted jobs and retained immutable pixel data. */
#include "cache_state.hpp"

namespace atlas::ui_images::detail
{
/// @brief Atomic reservation makes concurrent shrink/release visible to the admission owner.
decoded_lease::decoded_lease(std::shared_ptr<decoded_budget> budget, std::size_t bytes)
    : budget_(std::move(budget))
{
    auto current = budget_->current.load();
    do
    {
        if (bytes > budget_->limit - current)
        {
            throw std::overflow_error("UI decoded image budget exhausted");
        }
    } while (!budget_->current.compare_exchange_weak(current, current + bytes));

    bytes_ = bytes;
    auto peak = budget_->peak.load();
    while (peak < current + bytes && !budget_->peak.compare_exchange_weak(peak, current + bytes))
    {
    }
}

/// @brief Destruction cannot race with shrink: the decoding wrapper retains its own shared lease.
decoded_lease::~decoded_lease()
{
    budget_->current.fetch_sub(bytes_);
}

/// @brief Charge actual vector capacity, not merely the meaningful RGBA payload size.
void decoded_lease::shrink(std::size_t actual)
{
    if (actual > bytes_)
    {
        throw std::length_error("decoder exceeded reserved pixel capacity");
    }

    budget_->current.fetch_sub(bytes_ - actual);
    bytes_ = actual;
}

/// @brief Returned aliases keep the reservation even after requests, cache and old frames change.
std::shared_ptr<const renderer_content::image_data>
leased_decoder::decode(std::span<const std::uint8_t> encoded, renderer_content::image_limits limits,
                       std::shared_ptr<std::pmr::memory_resource> memory,
                       std::stop_token stop) const
{
    auto image = decoder_->decode(encoded, limits, std::move(memory), stop);
    if (!image)
    {
        throw std::runtime_error("decoder returned no UI image");
    }

    image->validate(limits);
    if (stop.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    lease_->shrink(image->pixels.capacity());

    struct pixel_owner
    {
        std::shared_ptr<decoded_lease> lease; ///< Retained until the final alias disappears.
        std::shared_ptr<const renderer_content::image_data>
            image; ///< Freed before its budget lease.
    }; ///< Shared ownership has a concrete lifetime purpose, not one control block per widget.

    auto owner = std::make_shared<pixel_owner>(pixel_owner{lease_, std::move(image)});
    const auto* pixels = owner->image.get();
    return {std::move(owner), pixels};
}
} // namespace atlas::ui_images::detail
