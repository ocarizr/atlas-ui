/** @file @brief UI event dispatch produces model demands without entering Lua from worker jobs. */
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
        "start",         "enable",         "disable",       "end",           "process_input",
        "tick",          "late_tick",      "end_frame",     "pointer_move",  "pointer_down",
        "pointer_up",    "pointer_cancel", "pointer_enter", "pointer_leave", "capture_lost",
        "key_down",      "key_up",         "focus_gained",  "focus_lost",    "action",
        "ensure_visible"};
    const auto kind = static_cast<std::size_t>(event.kind);
    if (kind >= names.size())
    {
        throw std::invalid_argument("invalid scripted UI event kind");
    }

    const diagnostics::operation_scope scope{destinations_, {"ui.script.event", "ui.dispatch"}};
    const std::array<mvvm::value, 8> args{
        static_cast<double>(event.delta_seconds),
        static_cast<std::int64_t>(event.phase),
        static_cast<double>(event.local.x),
        static_cast<double>(event.local.y),
        static_cast<std::int64_t>(event.key),
        event.action ? std::to_string(event.action->action) : std::string{},
        event.action ? static_cast<std::int64_t>(event.action->phase) : std::int64_t{-1},
        event.target_alive};
    const std::array<mvvm::value_type, 5> types{
        mvvm::value_type::boolean, mvvm::value_type::boolean, mvvm::value_type::boolean,
        mvvm::value_type::boolean, mvvm::value_type::boolean};
    const auto values = behavior_->call(names[kind], args, types);
    const auto flag = [&](std::size_t index)
    {
        return index < values.size() && std::get<bool>(values[index]);
    };
    return {flag(0), flag(1), flag(2), flag(3), flag(4)};
}
} // namespace atlas::ui_scripting
