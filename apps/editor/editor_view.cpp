/** @file @brief Editor owner loop for transactional commands and cached panel presentation. */
#include "editor_view.hpp"
#include "document_file.hpp"
#include "panel_scene.hpp"
#include "tool_panel.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Keep authored values, persistent file and render handoff on their proper owners.
class editor_view::impl
{
  public:
    /// @brief Borrow the application job pool and preserve the selected document path.
    impl(document_session& active, jobs::job_system& workers, std::filesystem::path path,
         const diagnostics::services& destinations, const tool_registry* sources)
        : session{active}, jobs{workers}, file{std::move(path)}, sinks{destinations},
          layout{jobs.dispatcher(), {}, {}, sinks}, paint{jobs.dispatcher(), {}, {}, sinks},
          tools{sources}
    {
        if (tools)
        {
            try
            {
                if (const auto& definition = tools->shell())
                {
                    shell = std::make_unique<tool_panel>(*definition, true, sinks);
                }
                else
                {
                    status = "Recovery mode";
                }
            }
            catch (const std::exception& error)
            {
                diagnostics::write(sinks, diagnostics::log_level::warning, "editor.shell",
                                   error.what());
                status = "Recovery mode";
            }
        }
    }

    void execute(const control_action& action); ///< Run only outside Atlas event callbacks.
    void rebuild(ui::layout_rect viewport, float scale); ///< Publish complete chrome candidate.
    void cycle_tool(); ///< Switch to the next valid contribution or the preview.
    bool dispatch(std::span<const input::event> events); ///< Process isolated Lua and host actions.

    document_session& session;           ///< Main-thread authored state.
    jobs::job_system& jobs;              ///< Used for native streamer reads and joined layout work.
    std::filesystem::path file;          ///< Explicit host-selected document file.
    diagnostics::services sinks;         ///< Logs and profile destinations.
    ui::layout_engine layout;            ///< Shared engine, reused by chrome and preview.
    ui::paint_engine paint;              ///< Shared ordered paint preparation.
    std::unique_ptr<panel_scene> chrome; ///< Cached controls and immutable packet.
    ui::layout_rect viewport;            ///< Last logical extent.
    float density = 0;                   ///< Last physical/logical ratio.
    std::size_t page = 0;                ///< Hierarchy page.
    std::string status;                  ///< Short ASCII result for the toolbar.
    const tool_registry* tools;          ///< Origin mounts outlive every panel.
    std::unique_ptr<tool_panel> shell;   ///< Authored editor chrome and its Lua behavior.
    bool shell_visible = false;          ///< Authored shell owns input only when it is arranged.
    std::unique_ptr<tool_panel> active_tool; ///< Current independent contribution.
    std::size_t tool_index = 0;              ///< Zero displays the document preview.
    ui::layout_rect tool_bounds;             ///< Last arranged contribution viewport.
};

/// @brief Build the next tool privately and skip invalid contributions.
void editor_view::impl::cycle_tool()
{
    if (!tools)
    {
        return;
    }

    const auto entries = tools->tools();
    for (auto next = tool_index + 1; next <= entries.size(); ++next)
    {
        try
        {
            auto candidate = std::make_unique<tool_panel>(entries[next - 1], false, sinks);
            (void)candidate->paint(tool_bounds, layout, paint);
            active_tool = std::move(candidate);
            tool_index = next;
            return;
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.tools",
                               error.what());
            status = "Invalid tool skipped";
        }
    }

    active_tool.reset();
    tool_index = 0;
}

/// @brief Apply owned actions after all live input dispatch has completed.
bool editor_view::impl::dispatch(std::span<const input::event> events)
{
    auto commands = chrome->dispatch(events);
    bool changed = false;
    const auto route = [&](std::unique_ptr<tool_panel>& panel, bool is_shell)
    {
        try
        {
            auto result = panel->dispatch(events, density);
            changed |= result.changed;
            commands.insert(commands.end(), std::make_move_iterator(result.commands.begin()),
                            std::make_move_iterator(result.commands.end()));
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.tools",
                               error.what());
            panel.reset();
            changed = true;
            status = is_shell ? "Recovery mode" : "Tool stopped";
            if (!is_shell)
            {
                tool_index = 0;
            }
        }
    };
    if (shell_visible)
    {
        route(shell, true);
    }

    if (active_tool)
    {
        route(active_tool, false);
    }

    for (const auto& command : commands)
    {
        execute(command);
    }

    return changed || !commands.empty();
}

/// @brief Dispatch editor operations and preserve active state when persistence or editing fails.
void editor_view::impl::execute(const control_action& action)
{
    const diagnostics::operation_scope scope{sinks, {"editor.command", "editor"}};
    try
    {
        status.clear();
        switch (action.kind)
        {
            case control_kind::select:
                session.select(action.target);
                break;
            case control_kind::add_child:
                if (session.apply(edit_kind::add_child))
                {
                    page = (session.document().elements.size() - 1) / hierarchy_page_size;
                }
                break;
            case control_kind::remove:
                (void)session.apply(edit_kind::remove);
                break;
            case control_kind::narrower:
                (void)session.apply(edit_kind::narrower);
                break;
            case control_kind::wider:
                (void)session.apply(edit_kind::wider);
                break;
            case control_kind::toggle_visible:
                (void)session.apply(edit_kind::toggle_visible);
                break;
            case control_kind::undo:
                (void)session.undo();
                break;
            case control_kind::redo:
                (void)session.redo();
                break;
            case control_kind::new_document:
                session.replace(new_document(), false);
                page = 0;
                break;
            case control_kind::save:
                save_document(file, session.document(), sinks);
                session.mark_saved();
                status = "Document saved";
                break;
            case control_kind::reopen:
                session.replace(open_document(file, jobs, sinks), true);
                page = 0;
                status = "Document reopened";
                break;
            case control_kind::previous:
                if (page != 0)
                {
                    --page;
                }
                break;
            case control_kind::next:
                ++page;
                break;
            case control_kind::cycle_tool:
                cycle_tool();
                break;
            case control_kind::none:
                break;
        }
    }
    catch (const std::exception& error)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor", error.what());
        status = "Operation failed. See log.";
    }
}

/// @brief Build the render packet only when document, viewport or control state changes.
void editor_view::impl::rebuild(ui::layout_rect bounds, float scale)
{
    const diagnostics::operation_scope scope{sinks, {"editor.panels", "editor"}};
    page = std::min(page, (session.document().elements.size() - 1) / hierarchy_page_size);
    std::optional<shell_areas> areas;
    ui::paint_snapshot shell_paint;
    if (shell && bounds.width >= 520 && bounds.height >= 240)
    {
        try
        {
            shell_paint = shell->paint(bounds, layout, paint);
            areas = shell_areas{shell->area("hierarchy"), shell->area("inspector"),
                                shell->area("preview")};
            if (areas->hierarchy.width < 100 || areas->inspector.width < 100 ||
                areas->preview.width < 32)
            {
                throw std::invalid_argument("Editor shell panel slots are too small");
            }
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.shell",
                               error.what());
            shell.reset();
            areas.reset();
            shell_paint = {};
            status = "Recovery mode";
        }
    }

    auto plan = make_panels(session, {bounds.width, bounds.height}, page, status,
                            areas ? &*areas : nullptr);
    plan.shell = std::move(shell_paint);
    shell_visible = areas.has_value();
    if (shell_visible)
    {
        shell->append_labels(plan.controls);
    }

    tool_bounds = plan.preview;
    if (active_tool)
    {
        plan.tool = active_tool->paint(tool_bounds, layout, paint);
        active_tool->append_labels(plan.controls);
    }

    if (tools && !tools->tools().empty())
    {
        const auto title =
            tool_index == 0 ? std::string{"Tools"} : tools->tools()[tool_index - 1].title;
        plan.controls.push_back(
            {{plan.preview.x + 88, plan.preview.y - 28, std::max(1.f, plan.preview.width - 96), 24},
             {0.14f, 0.21f, 0.30f, 1},
             title,
             {control_kind::cycle_tool, {}}});
    }

    chrome =
        std::make_unique<panel_scene>(plan, session.preview(), bounds, scale, layout, paint, sinks);
    viewport = bounds;
    density = scale;
}

/// @brief Initialize reusable layout/paint services on the main owner.
editor_view::editor_view(document_session& session, jobs::job_system& jobs,
                         std::filesystem::path file, const diagnostics::services& sinks,
                         const tool_registry* tools)
    : impl_{std::make_unique<impl>(session, jobs, std::move(file), sinks, tools)}
{
}

/// @brief Release main-owned input state after the render loop drains retained packets.
editor_view::~editor_view() = default;

/// @brief Update layout for window changes, then route input and publish any resulting changes.
std::shared_ptr<const editor_frame> editor_view::snapshot(const window::window_state& state,
                                                          std::span<const input::event> events)
{
    const auto density = std::clamp(state.dpi_scale, 0.5f, 8.f);
    const ui::layout_rect bounds{0, 0, static_cast<float>(state.width) / density,
                                 static_cast<float>(state.height) / density};
    if (!impl_->chrome || bounds != impl_->viewport || density != impl_->density)
    {
        impl_->rebuild(bounds, density);
    }

    if (!events.empty() && impl_->dispatch(events))
    {
        impl_->rebuild(bounds, density);
    }

    return impl_->chrome->packet();
}
} // namespace atlas::editor
