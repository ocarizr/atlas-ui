/** @file @brief UI event dispatch produces model demands without entering Lua from worker jobs. */
#include "drag_protocol.hpp"
#include <array>
#include <atlas/ui_scripting/event_handler.hpp>
#include <stdexcept>

namespace atlas::ui_scripting
{
/// @brief Retain a nonnull behavior session created on the same owner thread.
event_handler::event_handler(std::shared_ptr<scripting::session> behavior,
                             diagnostics::services destinations)
    : behavior_(std::move(behavior)), destinations_(std::move(destinations))
{
    if (!behavior_)
    {
        throw std::invalid_argument("UI scripting needs a behavior session");
    }

    (void)behavior_->inspect();
}

/// @brief Release the retained behavior outside active UI dispatch.
event_handler::event_handler(std::shared_ptr<scripting::domain> runtime,
                             scripting::instance_handle instance,
                             diagnostics::services destinations)
    : domain_(std::move(runtime)), instance_(instance), destinations_(std::move(destinations))
{
    if (!domain_)
    {
        throw std::invalid_argument("UI behavior needs a domain");
    }

    (void)domain_->inspect(instance_);
}

/// @brief Release the retained behavior outside active UI dispatch.
event_handler::~event_handler()
{
    if (owner_ != std::this_thread::get_id())
    {
        std::terminate();
    }
}

/// @brief Forward scalar lifecycle/input metadata without producing direct tree writes.
ui::event_response event_handler::on_event(const ui::routed_event& event, ui::command_buffer&)
{
    if (owner_ != std::this_thread::get_id())
    {
        throw std::logic_error("UI behavior requires its owner thread");
    }

    static constexpr std::array names{
        "start",          "enable",         "disable",       "end",           "process_input",
        "tick",           "late_tick",      "end_frame",     "pointer_move",  "pointer_down",
        "pointer_up",     "pointer_cancel", "pointer_enter", "pointer_leave", "capture_lost",
        "key_down",       "key_up",         "focus_gained",  "focus_lost",    "action",
        "ensure_visible", "pointer_scroll", "activate",      "drag_prepare",  "drag_started",
        "drag_query",     "drag_enter",     "drag_over",     "drag_leave",    "drag_drop",
        "drag_completed", "drag_cancelled"};
    const auto kind = static_cast<std::size_t>(event.kind);
    if (kind >= names.size())
    {
        throw std::invalid_argument("invalid scripted UI event kind");
    }

    const diagnostics::operation_scope scope{destinations_, {"ui.script.event", "ui.dispatch"}};
    const auto args = detail::arguments(event);
    const bool drag = event.kind >= ui::event_kind::drag_prepare;
    const auto arguments = std::span<const mvvm::value>{args}.first(drag ? 18 : 10);
    const std::array<mvvm::value_type, 9> types{
        mvvm::value_type::boolean, mvvm::value_type::boolean, mvvm::value_type::boolean,
        mvvm::value_type::boolean, mvvm::value_type::boolean, mvvm::value_type::text,
        mvvm::value_type::text,    mvvm::value_type::integer, mvvm::value_type::integer};
    ui::event_response response;
    const auto results = std::span<const mvvm::value_type>{types}.first(drag ? 9 : 5);
    const scripting::result_validator validate = [&](std::span<const mvvm::value> values)
    {
        response = detail::response(values, event.kind);
    };
    if (domain_)
    {
        (void)domain_->call(instance_, names[kind], arguments, results, validate);
    }
    else
    {
        (void)behavior_->call(names[kind], arguments, results, validate);
    }

    return response;
}
} // namespace atlas::ui_scripting
