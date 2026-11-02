/**
 * @file
 * @brief Buffer-only image decoding with explicit output limits and allocator ownership.
 */
#pragma once
#include <atlas/streaming/asset_streamer.hpp>
#include <span>

namespace atlas::renderer_content
{
/// @brief Per-image limits checked before allocating decoded pixels.
struct image_limits
{
    std::uint32_t max_dimension = 16384; ///< Maximum supported width or height.
    std::size_t max_decoded_bytes = std::size_t{64} * 1024 * 1024; ///< RGBA8 output ceiling.
    void validate() const; ///< Reject zero or unsupported limits.
};

/// @brief Tight straight-alpha RGBA8; allocation policy stays alive until pixels are freed.
struct image_data
{
    explicit image_data(std::shared_ptr<std::pmr::memory_resource> resource =
                            {}); ///< Retain allocator before constructing pixels.
    image_data(const image_data&) =
        delete; ///< Share immutable payload ownership instead of copying.
    image_data&
    operator=(const image_data&) = delete; ///< Preserve allocator/storage destruction order.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Retained thread-safe allocator.
    std::pmr::vector<std::byte> pixels;                ///< Top-to-bottom rows with no padding.
    std::uint32_t width = 0;                           ///< Decoded horizontal extent.
    std::uint32_t height = 0;                          ///< Decoded vertical extent.
    void validate(image_limits limits = {}) const;     ///< Require exact bounded RGBA storage.
};

/// @brief Reentrant decoder interface; implementations may not open storage or retain input spans.
class image_decoder
{
  public:
    virtual ~image_decoder() = default; ///< Release after all accepted decodes finish.
    virtual std::shared_ptr<const image_data>
    decode(std::span<const std::uint8_t> encoded, image_limits limits,
           std::shared_ptr<std::pmr::memory_resource> memory, std::stop_token stop)
        const = 0; ///< Check caps before output allocation and cooperate with cancellation.
};

/// @brief Select the native buffer-only decoder; SDK-free builds report unavailability.
std::shared_ptr<const image_decoder> make_image_decoder();
} // namespace atlas::renderer_content
