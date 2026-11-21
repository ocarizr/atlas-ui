/** @file @brief Standard Atlas input capture drives bounded graph gestures after dispatch. */
#include "canvas_state.hpp"
#include <algorithm>

namespace atlas::editor_flow
{
namespace
{
/// @brief Queue pointer intents without mutating a graph while Atlas input is iterating.
class gesture_handler final : public ui::input_handler
{
  public:
    gesture_handler(item_ref item, std::vector<canvas_intent>& output)
        : item_{std::move(item)}, output_{output}
    {
    } ///< Borrow one canvas-owned queue.

    ui::event_response on_event(const ui::routed_event& event, ui::command_buffer&) override
    {
        if (event.phase != ui::route_phase::target && event.kind != ui::event_kind::capture_lost)
        {
            return {};
        }

        if (event.kind == ui::event_kind::pointer_down ||
            event.kind == ui::event_kind::pointer_move ||
            event.kind == ui::event_kind::pointer_up ||
            event.kind == ui::event_kind::pointer_cancel ||
            event.kind == ui::event_kind::capture_lost)
        {
            output_.push_back({item_, event.kind, {event.position.x, event.position.y}});
            return {.stop = true, .capture_pointer = event.kind == ui::event_kind::pointer_down};
        }

        return {};
    } ///< Capture is cancelled by normal Atlas lifetime/reset handling.
  private:
    item_ref item_;                      ///< Persistent graph identity, not a vector index.
    std::vector<canvas_intent>& output_; ///< Canvas-owned bounded queue.
};
} // namespace

/// @brief Allocate one bounded reusable tree and input queue through the host memory resource.
canvas::impl::impl(workspace& source_tool, diagnostics::services destinations,
                   std::shared_ptr<std::pmr::memory_resource> memory)
    : tool{source_tool}, sinks{std::move(destinations)},
      tree{{.max_elements = 4096}, memory, sinks},
      input{tree,
            {.max_nodes = 4096, .max_bindings = 257, .max_events = 4096},
            std::move(memory),
            sinks}
{
    pending.reserve(4096);
    root = tree.create();
    tree.set_panel(root, ui::canvas_layout{});
    ui::paint_style background;
    background.fill = background.clip = true;
    background.color = {0.035F, 0.05F, 0.07F, 1};
    tree.set_paint(root, background);
    input.bind(root, std::make_shared<gesture_handler>(item_ref{}, pending));
}

/// @brief Bind a node by graph identity so vector edits cannot retarget its handler.
ui::element_handle canvas::impl::create(item_ref item)
{
    const auto node = tree.create(root);
    tree.set_panel(node, ui::leaf_layout{});
    input.bind(node, std::make_shared<gesture_handler>(std::move(item), pending));
    return node;
}

/// @brief Borrow the graph owner and retain portable presentation state.
canvas::canvas(workspace& tool, diagnostics::services sinks,
               std::shared_ptr<std::pmr::memory_resource> memory)
    : impl_{std::make_unique<impl>(tool, std::move(sinks), std::move(memory))}
{
}

/// @brief Retire input before releasing tree storage and borrowed gesture queues.
canvas::~canvas() = default;

/// @brief Apply ordered value-only intents after the Atlas routing stack has unwound.
bool canvas::impl::apply(std::span<const canvas_intent> commands, bool extend, bool pan)
{
    bool changed = false;
    for (const auto& command : commands)
    {
        if (command.kind == ui::event_kind::pointer_down)
        {
            origin = last = command.position;
            moving = command.item.kind == item_kind::screen && !pan;
            panning = !moving;
            if (moving)
            {
                const auto selection = tool.selection();
                if (extend || std::ranges::find(selection, command.item) == selection.end())
                {
                    tool.select(command.item, extend);
                }

                if (!tool.selection().empty())
                {
                    tool.begin_move();
                }
                else
                {
                    moving = false;
                }
            }
            else if (!pan && !extend)
            {
                tool.select({});
            }

            changed = true;
        }
        else if (origin && command.kind == ui::event_kind::pointer_move)
        {
            if (moving)
            {
                tool.move_by({(command.position.x - origin->x) / tool.zoom(),
                              (command.position.y - origin->y) / tool.zoom()});
            }

            if (panning)
            {
                tool.pan({command.position.x - last.x, command.position.y - last.y});
            }

            last = command.position;
            changed = true;
        }
        else if (origin && (command.kind == ui::event_kind::pointer_up ||
                            command.kind == ui::event_kind::pointer_cancel ||
                            command.kind == ui::event_kind::capture_lost))
        {
            if (moving)
            {
                tool.end_move(command.kind != ui::event_kind::pointer_up);
            }

            origin.reset();
            moving = panning = false;
            changed = true;
        }
    }

    return changed;
}

/// @brief Route through persistent capture geometry, then commit or cancel owned gestures.
bool canvas::process(std::span<const ui::input_event> events, bool extend, bool pan)
{
    (void)impl_->tool.edit();
    if (!impl_->source.session())
    {
        return false;
    }

    const diagnostics::operation_scope scope{impl_->sinks, {"editor.flow.input", "editor.flow"}};
    impl_->pending.clear();
    impl_->input.begin_frame(++impl_->frame, impl_->source, impl_->geometry);
    impl_->input.process(events);
    impl_->input.tick();
    impl_->input.late_tick();
    impl_->input.end_frame();
    impl_->tree.apply(impl_->tree.commands().seal());
    return impl_->apply(impl_->pending, extend, pan);
}

/// @brief Borrow clipped text rectangles only until the next arrange operation.
std::span<const canvas_label> canvas::labels() const
{
    (void)impl_->tool.edit();
    return impl_->labels;
}
} // namespace atlas::editor_flow
