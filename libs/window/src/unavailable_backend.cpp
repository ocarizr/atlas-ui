/** @file @brief Explicit SDK-free backend rejection. */
#include "window_backend.hpp"

namespace atlas::window
{
/// @brief Report native backend availability without allocating graphics resources.
bool backend_available() noexcept
{
    return false;
}

/// @brief Construct the compiled platform adapter or report an unavailable backend.
std::shared_ptr<detail::window_backend> detail::make_window_backend(const window_options&)
{
    throw std::runtime_error("native window backend is unavailable in this build");
}
} // namespace atlas::window
