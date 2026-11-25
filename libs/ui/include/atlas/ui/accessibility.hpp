/**
 * @file
 * @brief Optional UI semantic descriptors and screen-space publication coordinates.
 */
#pragma once
#include <atlas/accessibility/channel.hpp>
#include <atlas/ui/commands.hpp>
#include <functional>
#include <string>

namespace atlas::ui
{
/// @brief Sparse authoring metadata; default images and generic elements are not exposed.
struct accessible_element
{
    accessibility::role role = accessibility::role::group; ///< Explicit known control semantics.
    std::string name{};                                    ///< Literal accessible name.
    std::string description{};                             ///< Literal additional help.
    std::string name_key{};        ///< Optional localization key, replaces literal at publication.
    std::string description_key{}; ///< Optional localization key for help.
    std::string value{};           ///< Current read-only textual value.
    std::uint32_t invoke_action = 0; ///< Existing semantic activation input action.
    std::uint32_t select_action = 0; ///< Existing semantic selection input action.
    bool selected = false; ///< Selection comes from application state, never inferred from focus.
    double number = 0;     ///< Read-only numeric current value.
    double minimum = 0;    ///< Numeric lower bound.
    double maximum = 1;    ///< Numeric upper bound.
    bool checked = false;  ///< Current application checkbox state.
};

/// @brief Client-to-screen conversion supplied by the window owner for one publication.
struct accessibility_view
{
    double x = 0;                ///< Client origin in physical screen pixels.
    double y = 0;                ///< Client origin in physical screen pixels.
    double scale = 1;            ///< Logical viewport units to physical screen pixels.
    element_handle modal_root{}; ///< When set, expose only this subtree.
};

/// @brief Owner-only localization resolution; never executed by native provider queries.
using accessible_text = std::function<std::string(std::string_view)>;
void validate_accessibility(const accessible_element& source); ///< Check metadata before binding.
} // namespace atlas::ui
