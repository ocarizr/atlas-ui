/** @file @brief Shared-window drag coordination over independently owned editor panels. */
#include "editor_view_state.hpp"
#include <algorithm>

namespace atlas::editor
{
namespace
{
/// @brief Cleanup failures are diagnostic and cannot replace the original dispatch exception.
void cancel_drag(ui::drag_drop_session& drag, const diagnostics::services& sinks)
{
    try
    {
        drag.cancel(ui::drag_cancel_reason::callback_failure);
    }
    catch (...)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor.drag",
                           "Drag cancellation cleanup failed");
    }

    try
    {
        drag.end_frame();
    }
    catch (...)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor.drag",
                           "Drag frame was not open during cleanup");
    }
}

/// @brief Faulted authored panels retire only after their shared drag lease is released.
tool_input_result finish_panels(std::span<std::unique_ptr<tool_panel>* const> panels,
                                const diagnostics::services& sinks)
{
    tool_input_result result;
    for (auto* panel : panels)
    {
        try
        {
            auto ready = (*panel)->finish_input();
            result.changed |= ready.changed;
            result.commands.insert(result.commands.end(),
                                   std::make_move_iterator(ready.commands.begin()),
                                   std::make_move_iterator(ready.commands.end()));
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.tools",
                               error.what());
            panel->reset();
            result.changed = true;
        }
    }

    return result;
}
} // namespace

/// @brief Retain chrome instead of copying its geometry for each pointer sample.
std::shared_ptr<const editor_frame> editor_view::impl::presentation()
{
    const auto feedback = drag.feedback();
    if (feedback.phase != ui::drag_phase::dragging && feedback.phase != ui::drag_phase::pending)
    {
        return chrome->packet();
    }

    auto frame = std::make_shared<editor_frame>();
    frame->base = chrome->packet();
    frame->viewport = frame->base->viewport;
    frame->atlas = frame->base->atlas;
    frame->drag = feedback;
    return frame;
}

/// @brief Native editor policies accept only their explicit stable-ID transfer types.
bool editor_view::impl::commit_drop(const ui::drop_request& request)
{
    if (!drag.can_commit(request.transfer))
    {
        return false;
    }

    const auto destination = chrome->destination(request.target);
    const diagnostics::operation_scope scope{sinks, {"editor.drag.commit", "editor"}};
    try
    {
        if (destination.kind == control_kind::select &&
            request.payload.type() == "atlas.editor.element" &&
            request.operation == ui::drag_operation::move)
        {
            return session().reparent(request.payload.text(), destination.target);
        }

        if (destination.kind == control_kind::drop_asset && assets &&
            request.payload.type() == "atlas.asset" &&
            request.operation == ui::drag_operation::copy)
        {
            assets->drop(assets::parse_id(request.payload.text()), destination.target, session(),
                         jobs, sinks);
            return true;
        }
    }
    catch (const std::exception& error)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor.drag", error.what());
        status = "Drop rejected";
    }

    return false;
}

/// @brief Execute completed intents after all live input callbacks and transfers finish.
void editor_view::impl::execute_controls(std::span<const control_action> commands)
{
    for (const auto& command : commands)
    {
        if (command.kind == control_kind::drop_asset)
        {
            continue;
        }

        const auto owner = workspace ? workspace->active_id() : 0;
        execute(command);
        if (workspace && owner != workspace->active_id())
        {
            break;
        }
    }
}

/// @brief Route once through the window domain; normal packets keep their previous view routing.
bool editor_view::impl::dispatch(std::span<const input::event> events)
{
    const diagnostics::operation_scope scope{sinks, {"editor.input", "editor"}};
    const auto packets = chrome->begin_input(events);
    std::vector<ui::input_session*> views{&chrome->input()};
    std::vector<std::unique_ptr<tool_panel>*> panels;
    bool changed = false;
    const auto admit = [&](std::unique_ptr<tool_panel>& panel)
    {
        try
        {
            panel->begin_input(!events.empty());
            views.push_back(&panel->input());
            panels.push_back(&panel);
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.tools",
                               error.what());
            panel.reset();
            changed = true;
        }
    };
    if (shell_visible && shell && !(flow && flow->visible))
    {
        admit(shell);
    }

    if (active_tool && !(assets && assets->visible) && !(flow && flow->visible))
    {
        admit(active_tool);
    }

    // Native chrome is the topmost control layer over authored tools.
    std::rotate(views.begin(), views.begin() + 1, views.end());
    try
    {
        drag.begin_frame(++input_frame, views);
        for (const auto& packet : packets)
        {
            if (!drag.process(packet))
            {
                for (auto* view : views)
                {
                    view->process_more(std::span{&packet, 1});
                }
            }
        }

        if (const auto request = drag.pending_drop())
        {
            const bool committed = commit_drop(*request);
            changed |= committed;
            drag.resolve(request->transfer, committed);
        }

        drag.end_frame();
    }
    catch (const std::exception& error)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor.drag", error.what());
        cancel_drag(drag, sinks);
        changed = true;
    }

    std::vector<control_action> commands;
    if (!chrome->input().statistics().failed)
    {
        commands = chrome->finish_input();
    }

    auto result = finish_panels(panels, sinks);
    changed |= result.changed;
    commands.insert(commands.end(), std::make_move_iterator(result.commands.begin()),
                    std::make_move_iterator(result.commands.end()));
    const bool dragging = drag.feedback().phase != ui::drag_phase::idle;
    changed |= !dragging && flow && flow->visible && flow->input(events, density);
    changed |= !dragging && !(flow && flow->visible) && assets && assets->visible &&
               assets->keyboard(events);
    execute_controls(commands);
    return changed || !commands.empty();
}
} // namespace atlas::editor
