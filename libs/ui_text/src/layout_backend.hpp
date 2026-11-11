/** @file @brief Backend contract selected by the build, with no SDK types. */
#pragma once
#include <atlas/ui_text/text_layout.hpp>

namespace atlas::ui_text::detail
{
[[nodiscard]] bool backend_available() noexcept; ///< Whether the selected backend can shape.
[[nodiscard]] text_layout
shape_native(const text_request& request); ///< Produce owned masks and geometry.
} // namespace atlas::ui_text::detail
