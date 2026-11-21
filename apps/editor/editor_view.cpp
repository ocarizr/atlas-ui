/** @file @brief Editor owner loop for transactional commands and cached panel presentation. */
#include "editor_view_state.hpp"
#include <algorithm>

namespace atlas::editor
{
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
    bool changed = flow && flow->visible && flow->input(events, density);
    changed |= !(flow && flow->visible) && assets && assets->visible && assets->keyboard(events);
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
    if (shell_visible && !(flow && flow->visible))
    {
        route(shell, true);
    }

    if (active_tool && !(assets && assets->visible) && !(flow && flow->visible))
    {
        route(active_tool, false);
    }

    for (const auto& command : commands)
    {
        const auto owner = workspace ? workspace->active_id() : 0;
        execute(command);
        if (workspace && owner != workspace->active_id())
        {
            break;
        }
    }

    return changed || !commands.empty();
}

/// @brief Compose the graph tool independently from ordinary document panels.
void editor_view::impl::compose_flow(panel_plan& plan, ui::layout_rect bounds)
{
    if (!flow)
    {
        return;
    }

    if (flow->visible)
    {
        plan.controls.clear();
        plan.shell = {};
        plan.preview = {8, 8, std::max(1.F, bounds.width - 16), std::max(1.F, bounds.height - 42)};
        flow->append(plan, layout, paint, session());
    }

    plan.controls.push_back({{bounds.width - 156, bounds.height - 28, 72, 24},
                             {0.14F, 0.21F, 0.30F, 1},
                             flow->visible ? "UI view" : "Flow",
                             {control_kind::flow_toggle, {}}});
}

/// @brief Build the render packet only when document, viewport or control state changes.
void editor_view::impl::rebuild(ui::layout_rect bounds, float scale)
{
    const diagnostics::operation_scope scope{sinks, {"editor.panels", "editor"}};
    page = std::min(page, (session().document().elements.size() - 1) / hierarchy_page_size);
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

    auto plan = make_panels(session(), {bounds.width, bounds.height}, page, status,
                            areas ? &*areas : nullptr);
    plan.shell = std::move(shell_paint);
    if (workspace)
    {
        const auto y = bounds.height - 28;
        plan.preview.height = std::max(1.0F, plan.preview.height - 28);
        plan.controls.push_back({{8, y, 100, 24},
                                 {0.14F, 0.21F, 0.30F, 1},
                                 "Next document",
                                 {control_kind::cycle_document, {}}});
        plan.controls.push_back({{114, y, 64, 24},
                                 {0.14F, 0.21F, 0.30F, 1},
                                 "Close",
                                 {control_kind::close_document, {}}});
        plan.controls.push_back({{184, y, 72, 24},
                                 {0.14F, 0.21F, 0.30F, 1},
                                 "Recover",
                                 {control_kind::recover_document, {}}});
        plan.controls.push_back(
            {{264, y, std::max(1.0F, bounds.width - 272), 24},
             {0, 0, 0, 0},
             path().filename().string() +
                 (workspace->recovery_pending() ? " | Recovery available" : "")});
    }

    shell_visible = areas.has_value();
    if (shell_visible)
    {
        shell->append_labels(plan.controls);
    }

    tool_bounds = plan.preview;
    if (active_tool && !(assets && assets->visible) && !(flow && flow->visible))
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

    if (assets && !(flow && flow->visible))
    {
        plan.controls.push_back({{bounds.width - 80, bounds.height - 28, 72, 24},
                                 {0.14F, 0.21F, 0.30F, 1},
                                 "Assets",
                                 {control_kind::assets_toggle, {}}});
        if (assets->visible)
        {
            assets->append(plan);
        }
    }

    compose_flow(plan, bounds);

    chrome = std::make_unique<panel_scene>(plan, session().preview(), bounds, scale, layout, paint,
                                           sinks);
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

/// @brief The application retains repository services through every panel callback and frame.
void editor_view::bind_assets(asset_repository& repository, std::filesystem::path root)
{
    impl_->assets = std::make_unique<asset_panel>(repository, std::move(root));
    impl_->chrome.reset();
}

/// @brief Persistent graph storage remains owned by the application, not the visual panel.
void editor_view::bind_flow(editor_flow::persistence& document, asset_repository* repository,
                            std::shared_ptr<std::pmr::memory_resource> memory,
                            const std::string& identity_scope)
{
    impl_->flow = std::make_unique<flow_panel>(document, impl_->sinks, repository,
                                               std::move(memory), identity_scope);
    impl_->chrome.reset();
}

/// @brief Bounded discovery never invokes an action or condition implementation.
void editor_view::bind_flow_module(std::span<const scripting::source_asset> sources,
                                   scripting::callable_module entry)
{
    if (!impl_->flow)
    {
        throw std::logic_error("Bind a Flow document before callable discovery");
    }

    impl_->flow->register_module(sources, std::move(entry));
    impl_->chrome.reset();
}

/// @brief The workspace remains the sole owner of documents as selection changes.
editor_view::editor_view(document_workspace& workspace, jobs::job_system& jobs,
                         const diagnostics::services& sinks, const tool_registry* tools)
    : editor_view{workspace.active(), jobs, workspace.path(), sinks, tools}
{
    impl_->workspace = &workspace;
    impl_->legacy_session = nullptr;
}

/// @brief Update layout for window changes, then route input and publish any resulting changes.
std::shared_ptr<const editor_frame> editor_view::snapshot(const window::window_state& state,
                                                          std::span<const input::event> events)
{
    const auto density = std::clamp(state.dpi_scale, 0.5f, 8.f);
    const ui::layout_rect bounds{0, 0, static_cast<float>(state.width) / density,
                                 static_cast<float>(state.height) / density};
    if (impl_->assets && impl_->assets->poll(impl_->workspace, impl_->jobs, impl_->sinks))
    {
        impl_->chrome.reset();
    }

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
