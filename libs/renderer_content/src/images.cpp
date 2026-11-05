/** @file @brief Bounded portable image payload validation. */
#include <atlas/renderer_content/images.hpp>
#include <stdexcept>

namespace atlas::renderer_content
{
/// @brief Keep native dimensions and payload arithmetic representable.
void image_limits::validate() const
{
    if (max_dimension == 0 || max_dimension > 16384 || max_decoded_bytes == 0)
    {
        throw std::invalid_argument("invalid image decode limits");
    }
}

/// @brief Retain allocator lifetime ahead of pixel storage in member destruction order.
image_data::image_data(std::shared_ptr<std::pmr::memory_resource> resource)
    : memory(std::move(resource)),
      pixels(0, memory ? memory.get() : std::pmr::new_delete_resource())
{
}

/// @brief Check output caps and exact tight RGBA8 size without overflowing host arithmetic.
void image_data::validate(image_limits limits) const
{
    limits.validate();
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("empty image dimensions");
    }

    const auto bytes = std::uint64_t{width} * height * 4;
    if (width > limits.max_dimension || height > limits.max_dimension ||
        bytes > limits.max_decoded_bytes)
    {
        throw std::length_error("decoded image exceeds output limits");
    }

    if (bytes != pixels.size())
    {
        throw std::invalid_argument("image requires exact RGBA8 rows");
    }
}
} // namespace atlas::renderer_content
