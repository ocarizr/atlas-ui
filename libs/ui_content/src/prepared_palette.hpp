/** @file @brief Prepared v11 retains semantic tint names for one-time runtime slot resolution. */
#pragma once
#include "prepared_binary.hpp"

namespace atlas::ui_content::detail
{
/// @brief Append only to v11; older documents preserve their exact binary representation.
template <class Archive> void palette_fields(Archive& archive, document& source)
{
    for (auto& node : source.elements)
    {
        archive.fields(node.color_key, node.material_color_key);
    }
}
} // namespace atlas::ui_content::detail
