/** @file @brief Resolve popup defaults and validate typed region-host contracts before commit. */
#include "flow_state.hpp"
#include <algorithm>

namespace atlas::ui_content
{
/// @brief Unspecified instance fields continue inheriting their asset's defaults.
popup_policy resolve_popup(popup_policy defaults, const popup_overrides& overrides)
{
    if (overrides.modal)
    {
        defaults.modal = *overrides.modal;
    }

    if (overrides.dismiss_outside)
    {
        defaults.dismiss_outside = *overrides.dismiss_outside;
    }

    if (overrides.dismiss_back)
    {
        defaults.dismiss_back = *overrides.dismiss_back;
    }

    return defaults;
}

/// @brief Legacy overlay presentations retain their established modal behavior.
std::optional<popup_policy> flow_manager::impl::popup(const detail::flow_entry& entry) const
{
    const auto& source = graph.screens.at(entry.screen);
    if (source.asset.type != assets::asset_type::popup)
    {
        return entry.overlay ? std::optional{popup_policy{}} : std::nullopt;
    }

    if (!entry.session)
    {
        return std::nullopt;
    }

    return resolve_popup(entry.session->view->presentation().popup, source.popup);
}

/// @brief Parent hosts are validated from the exact composed candidate, never from source paths.
void flow_manager::impl::validate_view(const detail::flow_pending& pending, instance& view) const
{
    const diagnostics::operation_scope scope{diagnostics, {"ui.flow.presentation", "ui.content"}};
    if (!candidate)
    {
        throw std::logic_error("Missing Flow presentation candidate");
    }

    const auto& entry = candidate->stacks.at(pending.position.region).at(pending.position.index);
    const auto& source = graph.screens.at(entry.screen);
    validate_presentation(view.presentation(), source.asset.type);
    for (const auto& region : graph.regions)
    {
        if (region.owner == source.id && !region.host.empty())
        {
            const auto host = view.find(region.host);
            if (std::holds_alternative<ui::leaf_layout>(view.tree().panel(host)))
            {
                throw std::invalid_argument("Flow submenu host must be a panel: " + region.host);
            }
        }
    }
}
} // namespace atlas::ui_content
