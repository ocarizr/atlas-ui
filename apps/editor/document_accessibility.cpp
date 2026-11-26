/** @file @brief Accessibility edits reuse the validated source/preview history transaction. */
#include "document_session.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Validate a complete semantic edit before publishing history or preview changes.
bool document_session::set_accessibility(std::optional<ui::accessible_element> descriptor,
                                         bool decorative)
{
    if (decorative && descriptor)
    {
        throw std::invalid_argument("decorative elements cannot declare accessibility overrides");
    }

    auto source = document();
    const auto target = std::ranges::find(source.elements, selected(), &ui_content::element::id);
    if (target->accessible == descriptor && target->decorative == decorative)
    {
        return false;
    }

    const auto selection = std::string{selected()};
    target->accessible = std::move(descriptor);
    target->decorative = decorative;
    restore(std::move(source));
    select(selection);
    return true;
}
} // namespace atlas::editor
