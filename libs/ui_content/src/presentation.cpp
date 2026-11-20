/** @file @brief Validate explicit UI kinds and retain popup presentation on composed instances. */
#include "instance_state.hpp"
#include <stdexcept>

namespace atlas::ui_content
{
/// @brief Legacy references remain legacy; converting an asset requires an explicit source update.
void validate_presentation(presentation_options options, std::optional<assets::asset_type> expected)
{
    if (!assets::is_ui(options.type) ||
        (options.type != assets::asset_type::popup && options.popup != popup_policy{}))
    {
        throw std::invalid_argument("Invalid UI presentation kind or popup-only policy");
    }

    if (expected && (!assets::is_ui(*expected) || *expected != options.type))
    {
        throw std::invalid_argument("UI presentation kind differs from its catalog type");
    }
}

/// @brief Borrow no authoring storage when returning immutable presentation defaults.
presentation_options instance::presentation() const
{
    return impl_->presentation;
}
} // namespace atlas::ui_content
