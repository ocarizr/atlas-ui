/** @file @brief Destination-free menu and popup close actions respect active ownership. */
#include "flow_state.hpp"
#include <algorithm>

namespace atlas::ui_content
{
/// @brief Queue a scoped action using the same single-admission contract as graph routes.
bool flow_manager::exit_menu(std::string_view source)
{
    impl_->check();
    auto& admission = *impl_->admission;
    if (admission.busy || admission.command || source.empty() || source.size() > 64)
    {
        return false;
    }

    admission.command = detail::flow_command{{}, 0, std::string{source}, flow_operation::exit_menu};
    return true;
}

/// @brief A deliberate popup button close is separate from Back/outside dismissal policy.
bool flow_manager::close_popup(std::string_view source)
{
    impl_->check();
    auto& admission = *impl_->admission;
    if (admission.busy || admission.command || source.empty() || source.size() > 64)
    {
        return false;
    }

    admission.command =
        detail::flow_command{{}, 0, std::string{source}, flow_operation::close_popup};
    return true;
}

/// @brief Exit the enclosing menu once; submenu tab history is never interpreted as menu history.
bool flow_manager::impl::apply_exit(detail::flow_candidate& next, std::string_view source,
                                    flow_operation operation)
{
    const auto found = std::ranges::find(graph.screens, source, &flow_screen::id);
    if (found == graph.screens.end())
    {
        return false;
    }

    const auto source_index = static_cast<std::size_t>(found - graph.screens.begin());
    const auto shown = visible(current);
    if (std::ranges::none_of(shown,
                             [&](const auto& position)
                             {
                                 return position.input &&
                                        current.at(position.region).at(position.index).screen ==
                                            source_index;
                             }))
    {
        return false;
    }

    auto owner = region(found->region);
    if (!scope_history(next.stacks.at(owner), source_index))
    {
        return false;
    }

    while (!graph.regions.at(owner).owner.empty() && !popup(next.stacks.at(owner).back()))
    {
        const auto parent = screen(graph.regions.at(owner).owner);
        owner = region(graph.screens.at(parent).region);
        if (!scope_history(next.stacks.at(owner), parent))
        {
            return false;
        }
    }

    const auto& entry = next.stacks.at(owner).back();
    if (operation == flow_operation::close_popup)
    {
        if (!popup(entry) || !entry.overlay)
        {
            return false;
        }
    }
    else if (popup(entry))
    {
        return false;
    }

    auto& stack = next.stacks.at(owner);
    if (stack.size() < 2)
    {
        diagnostics::write(diagnostics, diagnostics::log_level::warning, "ui.flow",
                           "Scoped exit rejected: no navigation history to restore");
        return false;
    }

    stack.pop_back();
    return true;
}
} // namespace atlas::ui_content
