/** @file @brief Owned preview diagnostics and explicitly structural bounded traversal planning. */
#include "preview_state.hpp"
#include <algorithm>

namespace atlas::editor_flow
{
/// @brief Inspector choices always refer to the applied graph, even while its draft changes.
std::vector<std::string> preview::routes() const
{
    impl_->check();
    std::vector<std::string> result;
    if (impl_->active)
    {
        for (const auto& edge : impl_->active->candidate.graph().transitions)
        {
            result.push_back(edge.id);
        }
    }

    return result;
}

bool preview::running() const
{
    impl_->check();
    return impl_->active != nullptr;
} ///< Active snapshot.

bool preview::applying() const
{
    impl_->check();
    return impl_->pending != nullptr;
} ///< Private apply.

std::string_view preview::diagnostic() const
{
    impl_->check();
    return impl_->diagnostic;
} ///< Last result.

ui_content::flow_status preview::status() const
{
    impl_->check();
    return impl_->status;
} ///< Last sync.

std::string_view preview::active_route() const
{
    impl_->check();
    return impl_->active ? std::string_view{impl_->active->route} : std::string_view{};
} ///< Exact route.

/// @brief Copy diagnostics before the next publication invalidates borrowed views.
std::vector<preview_screen> preview::presentation() const
{
    impl_->check();
    std::vector<preview_screen> result;
    if (impl_->active)
    {
        for (const auto& item : impl_->active->flow->presentation())
        {
            result.push_back({std::string{item.region}, std::string{item.screen},
                              item.view->authored_id(item.view->input().focused()), item.input,
                              item.popup.has_value()});
        }
    }

    return result;
}

/// @brief Inactive history entries retain names rather than complete UI/Lua state.
std::vector<std::pair<std::string, std::vector<std::string>>> preview::history() const
{
    impl_->check();
    return impl_->active ? impl_->active->flow->history()
                         : std::vector<std::pair<std::string, std::vector<std::string>>>{};
}

/// @brief Guard diagnostics reflect real invocations, never guessed structural reachability.
std::vector<std::pair<std::string, bool>> preview::conditions() const
{
    impl_->check();
    return impl_->active ? impl_->active->evaluated : std::vector<std::pair<std::string, bool>>{};
}

/// @brief The runtime graph caps traversal; arbitrary Lua conditions still require fixtures/replay.
traversal_plan preview::plan(std::string_view from, std::string_view to) const
{
    impl_->check();
    if (!impl_->active)
    {
        throw std::logic_error("Traversal requires an applied preview");
    }

    traversal_plan result;
    result.routes = ui_content::plan_flow_path(impl_->active->candidate.graph(), from, to);
    for (const auto& route : result.routes)
    {
        if (std::ranges::any_of(impl_->active->candidate.conditions(),
                                [&](const auto& value)
                                {
                                    return value.transition == route;
                                }))
        {
            result.conditioned.push_back(route);
        }
    }

    return result;
}
} // namespace atlas::editor_flow
