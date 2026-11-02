/** @file @brief SDK-free decoder selection reports explicit native unavailability. */
#include <atlas/renderer_content/images.hpp>
#include <stdexcept>

namespace atlas::renderer_content
{
/// @brief Portable consumers can inject a buffer decoder without acquiring Windows SDK types.
std::shared_ptr<const image_decoder> make_image_decoder()
{
    throw std::runtime_error("native image decoding is unavailable in the SDK-free profile");
}
} // namespace atlas::renderer_content
