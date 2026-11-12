/** @file @brief Compose XML and Lua using the shared UI runtime and explicit owner phases. */
#include "tool_panel.hpp"
#include <algorithm>
#include <utility>

namespace atlas::editor
{
/// @brief Publish only bounded value commands; Lua never receives file or UI pointers.
class tool_panel::mailbox final : public scripting::synchronous_service
{
  public:
    /// @brief Reserve the broader document lifecycle commands for the installed shell.
    explicit mailbox(bool shell) : shell_{shell}
    {
    }

    /// @brief Admit only known actions while an editor input phase is active.
    std::vector<mvvm::value> invoke(std::span<const mvvm::value> arguments) override
    {
        const auto& command = std::get<std::string>(arguments.front());
        static constexpr std::array editing{
            std::pair{"add_child", control_kind::add_child},
            std::pair{"remove", control_kind::remove},
            std::pair{"narrower", control_kind::narrower}, std::pair{"wider", control_kind::wider},
            std::pair{"toggle_visible", control_kind::toggle_visible}};
        static constexpr std::array lifecycle{
            std::pair{"new", control_kind::new_document}, std::pair{"save", control_kind::save},
            std::pair{"reopen", control_kind::reopen}, std::pair{"undo", control_kind::undo},
            std::pair{"redo", control_kind::redo}};
        if (!accepting || commands.size() == 32)
        {
            throw std::logic_error("Editor commands require a bounded input phase");
        }

        for (const auto& [name, kind] : editing)
        {
            if (command == name)
            {
                commands.push_back({kind, {}});
                return {};
            }
        }

        if (shell_)
        {
            for (const auto& [name, kind] : lifecycle)
            {
                if (command == name)
                {
                    commands.push_back({kind, {}});
                    return {};
                }
            }
        }

        throw std::invalid_argument("Editor command unavailable to this tool");
    }

    std::vector<control_action> commands; ///< Unpublished actions of the current phase.
    bool accepting = false;               ///< Enable only during explicit input dispatch.

  private:
    bool shell_; ///< Installed shell privilege.
};

/// @brief Load through the origin mount and register host commands before Lua executes.
tool_panel::tool_panel(const tool_entry& source, bool shell, const diagnostics::services& sinks)
    : mailbox_{std::make_shared<mailbox>(shell)}
{
    const std::array services{
        scripting::service_definition{"editor.command", {mvvm::value_type::text}, {}, mailbox_}};
    const ui_content::construction options{
        .storage = *source.storage, .diagnostics = sinks, .services = services};
    const auto document = ui_content::load(source.reference, options);
    if (document.elements.size() > 256)
    {
        throw std::invalid_argument("Editor tool panels support at most 256 elements");
    }

    for (const auto& element : document.elements)
    {
        if (element.texture)
        {
            throw std::invalid_argument("Editor tool panels currently use solid geometry");
        }
    }

    view_ = ui_content::instance::build(document, options);
    for (const auto& element : document.elements)
    {
        if (!element.caption.empty())
        {
            labels_.emplace_back(view_->find(element.id), element.caption);
        }
    }
}

/// @brief Release the Lua owner before its retained editor command service.
tool_panel::~tool_panel() = default;

/// @brief Resolve an authored integration slot after layout publication.
ui::layout_rect tool_panel::area(std::string_view id) const
{
    const auto found = geometry_.find(view_->find(id));
    if (!found)
    {
        throw std::invalid_argument("Missing editor shell layout slot: " + std::string{id});
    }

    return found->bounds;
}

/// @brief Apply inherited visibility and clipping before publishing the authored caption.
void tool_panel::append_labels(std::vector<control>& target) const
{
    const auto& tree = view_->tree();
    for (const auto& [node, text] : labels_)
    {
        const auto bounds = geometry_.find(node);
        if (!bounds)
        {
            continue;
        }

        auto clip = ui::intersect_clip(bounds->bounds, geometry_.viewport());
        for (auto parent = node; parent.generation != 0; parent = tree.parent(parent))
        {
            if (!tree.requested(parent).visible)
            {
                clip.width = 0;
                break;
            }

            if (tree.paint(parent).clip)
            {
                if (const auto geometry = geometry_.find(parent))
                {
                    clip = ui::intersect_clip(clip, geometry->bounds);
                }
            }
        }

        if (clip.width > 0 && clip.height > 0)
        {
            target.push_back({clip, {0, 0, 0, 0}, text});
        }
    }
}

/// @brief Refresh immutable geometry and paint on the editor owner.
ui::paint_snapshot tool_panel::paint(ui::layout_rect bounds, ui::layout_engine& layout,
                                     ui::paint_engine& painter)
{
    auto& tree = view_->tree();
    geometry_ = layout.update(tree, bounds);
    source_ = tree.snapshot();
    if (frame_ == 1 && dispatch({}, 1).changed)
    {
        geometry_ = layout.update(tree, bounds);
        source_ = tree.snapshot();
    }

    return painter.calculate(source_, geometry_);
}

/// @brief Install startup subscriptions before routing the first pointer event.
tool_input_result tool_panel::dispatch(std::span<const input::event> events, float density)
{
    bridge_.clear();
    for (auto event : events)
    {
        if (event.device.kind == input::device_kind::pointer)
        {
            event.position.x /= density;
            event.position.y /= density;
            if (event.kind == input::event_kind::pointer_motion)
            {
                event.value.x /= density;
                event.value.y /= density;
            }
        }

        bridge_.on_input(event);
    }

    auto& input = view_->input();
    auto& tree = view_->tree();
    view_->begin_models(++frame_);
    input.begin_frame(frame_, source_, geometry_);
    view_->synchronize();
    mailbox_->accepting = !events.empty();
    try
    {
        input.process(bridge_.values());
        input.tick();
        input.late_tick();
        input.end_frame();
        view_->synchronize();
        const bool changed = tree.commands().pending() != 0;
        tree.apply(tree.commands().seal());
        mailbox_->accepting = false;
        return {std::exchange(mailbox_->commands, {}), changed};
    }
    catch (...)
    {
        mailbox_->accepting = false;
        mailbox_->commands.clear();
        throw;
    }
}
} // namespace atlas::editor
