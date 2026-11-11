/** @file @brief SDK-disabled build retains portable atlas operations and reports unavailable
 * shaping. */
#include "layout_backend.hpp"
#include <stdexcept>

namespace atlas::ui_text::detail
{
/// @brief This build has no platform text shaping service.
bool backend_available() noexcept
{
    return false;
}

/// @brief Fail explicitly for a nonempty request requiring native shaping.
text_layout shape_native(const text_request&)
{
    throw std::runtime_error("UI text shaping backend is unavailable in this build");
}
} // namespace atlas::ui_text::detail
