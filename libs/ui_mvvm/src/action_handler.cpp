/** @file @brief Owner-thread UI event/action mapping with prevalidated constant protocols. */
#include "binding_state.hpp"
#include <atlas/ui_mvvm/action_handler.hpp>
#include <stdexcept>

namespace atlas::ui_mvvm
{
/// @brief Compile authored names once while retaining the independent source model.
class action_handler::impl
{
  public:
    /// @brief One copied data route plus its validated action slot.
    struct route
    {
        action_binding_definition source; ///< Immutable trigger and constant payload.
        mvvm::action_handle action;       ///< Checked protocol slot.
    };

    std::shared_ptr<mvvm::view_model> model; ///< Mutable owner source of truth.
    std::vector<route> routes;               ///< At most 64 authored routes.
    diagnostics::services destinations;      ///< Optional profiling/log destinations.
    bool busy = false;                       ///< Reentry guard for synchronous diagnostic hooks.
    const std::thread::id owner = std::this_thread::get_id(); ///< Immutable dispatch affinity.
};

/// @brief Invalid authored routes fail before registration in the UI input session.
action_handler::action_handler(std::shared_ptr<mvvm::view_model> model,
                               std::span<const action_binding_definition> bindings,
                               diagnostics::services destinations)
    : impl_(std::make_unique<impl>())
{
    if (!model || bindings.size() > 64)
    {
        throw std::invalid_argument("invalid UI MVVM action handler configuration");
    }

    impl_->model = std::move(model);
    impl_->destinations = std::move(destinations);
    impl_->routes.reserve(bindings.size());
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.mvvm.actions.compile", "ui.cpu"}};
    for (const auto& binding : bindings)
    {
        if (binding.event > ui::event_kind::ensure_visible ||
            binding.route > ui::route_phase::bubble ||
            binding.input_phase > input::action_phase::cancelled ||
            ((binding.event == ui::event_kind::action) != (binding.input_action != 0)))
        {
            throw std::invalid_argument("invalid UI MVVM action event filter");
        }

        const auto action = impl_->model->description().action(binding.action);
        impl_->model->description().validate(action, binding.arguments);
        impl_->routes.push_back({binding, action});
    }
}

/// @brief Handler lifetime follows UI registration without introducing a model subscription cycle.
action_handler::~action_handler()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }
}

/// @brief Routing never mutates UI directly or calls the MVVM flush recursively.
ui::event_response action_handler::on_event(const ui::routed_event& event, ui::command_buffer&)
{
    if (impl_->owner != std::this_thread::get_id())
    {
        throw std::logic_error("UI MVVM action handler requires its owner");
    }

    if (impl_->busy)
    {
        throw std::logic_error("UI MVVM action handler is not reentrant");
    }

    const operation_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.mvvm.action", "ui.dispatch"}};
    for (const auto& route : impl_->routes)
    {
        const auto& filter = route.source;
        if (event.kind != filter.event || event.phase != filter.route)
        {
            continue;
        }

        if (filter.event == ui::event_kind::action &&
            (!event.action || event.action->action != filter.input_action ||
             event.action->phase != filter.input_phase))
        {
            continue;
        }

        impl_->model->invoke(route.action, filter.arguments);
    }

    return {};
}
} // namespace atlas::ui_mvvm
