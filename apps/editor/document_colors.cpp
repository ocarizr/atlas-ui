/** @file @brief Semantic color authoring reuses the document's validated undo transaction. */
#include "document_session.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Read the preview contract, never the editor chrome's independently selected palette.
std::vector<std::string> document_session::color_keys() const
{
    std::vector<std::string> result;
    if (options_.palette)
    {
        for (const auto& color : options_.palette->snapshot()->definition().colors)
        {
            result.push_back(color.key);
        }
    }

    return result;
}

/// @brief Unknown keys or a material binding on a nonmaterial element leave history unchanged.
bool document_session::set_color_key(std::string key, bool material)
{
    auto candidate = document();
    const auto target = std::ranges::find(candidate.elements, selected(), &ui_content::element::id);
    auto& field = material ? target->material_color_key : target->color_key;
    if (field == key)
    {
        return false;
    }

    const auto selection = std::string{selected()};
    field = std::move(key);
    restore(std::move(candidate));
    select(selection);
    return true;
}
} // namespace atlas::editor
