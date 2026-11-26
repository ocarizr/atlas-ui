/** @file @brief Explicit Atlas Flow preview controls, isolated runtime and owned diagnostics. */
#include "editor_theme.hpp"
#include "flow_panel.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Typed acquisition uses the application mount and shared allocator, never direct files.
void flow_panel::bind_preview(const jobs::job_dispatcher& jobs,
                              std::shared_ptr<streaming::catalog_streamer> storage,
                              std::shared_ptr<std::pmr::memory_resource> memory)
{
    if (!storage)
    {
        throw std::invalid_argument("Flow preview requires a typed storage mount");
    }

    preview_ = std::make_unique<editor_flow::preview>(jobs, memory, sinks_);
    preview_memory_ = std::move(memory);
    preview_storage_ = std::move(storage);
}

/// @brief Freeze discovered modules on explicit apply; live authoring never mutates running Lua.
bool flow_panel::preview_command(std::string_view action)
{
    if (!action.starts_with("preview:"))
    {
        return false;
    }

    if (!preview_)
    {
        throw std::logic_error("Attach preview storage before applying a Flow");
    }

    const auto operation = action.substr(8);
    if (operation == "apply")
    {
        apply_preview();
    }
    else if (operation == "graph")
    {
        preview_visible_ = false;
    }
    else if (operation == "restart")
    {
        preview_->restart();
        preview_visible_ = true;
    }
    else if (operation == "show")
    {
        preview_visible_ = true;
    }
    else if (operation == "stop")
    {
        preview_->stop();
        preview_quads_.clear();
    }
    else if (operation == "cancel")
    {
        preview_->cancel();
    }
    else if (operation == "next-route")
    {
        ++preview_route_;
    }
    else if (operation == "route")
    {
        const auto routes = preview_->routes();
        if (!routes.empty())
        {
            (void)preview_->request(routes[preview_route_ % routes.size()]);
        }
    }
    else
    {
        return false;
    }

    return true;
}

/// @brief Snapshot current graph/catalog/registrations once, outside per-frame dispatch.
void flow_panel::apply_preview()
{
    if (preview_->applying())
    {
        preview_->cancel();
    }

    const auto mount = preview_mount_ ? preview_mount_() : preview_storage_;
    if (!mount)
    {
        throw std::runtime_error("Preview mount factory returned no storage");
    }

    const auto candidate = document_.edit().compile(
        [mount](auto reference)
        {
            try
            {
                (void)mount->describe(reference);
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        });
    const auto factory = ui_content::make_flow_loader(
        {.storage = *mount,
         .memory = preview_memory_,
         .diagnostics = sinks_,
         .palette = std::make_shared<ui::palette_context>(editor_preset("dark"))});
    ui_content::flow_loader loader = [mount, factory](auto request)
    {
        return factory(std::move(request));
    };
    const auto sources =
        std::make_shared<const std::vector<scripting::source_asset>>(preview_sources_);
    const auto modules =
        std::make_shared<const std::vector<scripting::callable_module>>(preview_modules_);
    preview_->apply(candidate, std::move(loader),
                    [sources, modules](auto& scripts)
                    {
                        for (const auto& source : *sources)
                        {
                            scripts.register_source(source);
                        }

                        for (const auto& module : *modules)
                        {
                            scripts.load_callable_module(module);
                        }
                    });
    preview_visible_ = true;
}

/// @brief Advance loading and UI frames without requiring mouse motion to refresh presentation.
bool flow_panel::pump_preview(std::span<const input::event> events, float density,
                              ui::accessibility_frame* accessibility,
                              const ui::accessible_text& resolve)
{
    if (!visible || !preview_visible_ || !preview_ || preview_bounds_.width <= 0)
    {
        preview_tick_ = {};
        return false;
    }

    application::ui_input_batch input{nullptr};
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

        input.on_input(event);
    }

    try
    {
        const auto now = std::chrono::steady_clock::now();
        const auto seconds = preview_tick_ == std::chrono::steady_clock::time_point{}
                                 ? 0.F
                                 : std::chrono::duration<float>(now - preview_tick_).count();
        preview_tick_ = now;
        preview_quads_ = preview_->frame(
            preview_bounds_, input.values(),
            {.delta_seconds = seconds, .accessibility = accessibility, .accessible_text = resolve});
    }
    catch (const std::exception& error)
    {
        diagnostics::write(sinks_, diagnostics::log_level::warning, "editor.flow.preview",
                           error.what());
        status_ = error.what();
        preview_->cancel();
    }

    return true;
}

/// @brief Bounded diagnostics use the same Atlas controls as every other editor panel.
void flow_panel::append_preview(panel_plan& plan)
{
    const auto bounds = plan.preview;
    preview_bounds_ = {bounds.x + 4, bounds.y + 54, std::max(1.F, bounds.width - 256),
                       std::max(1.F, bounds.height - 80)};
    plan.flow_preview = preview_quads_;
    const std::array actions{"graph", "apply", "restart", "cancel", "stop", "next-route", "route"};
    const std::array titles{"Graph", "Apply",      "Restart",       "Cancel load",
                            "Stop",  "Next route", "Traverse route"};
    const auto width = std::max(1.F, (bounds.width - 8) / 7 - 4);
    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        plan.controls.push_back(
            {{bounds.x + 4 + static_cast<float>(i) * (width + 4), bounds.y, width, 23},
             {0.14F, 0.21F, 0.30F, 1},
             titles[i],
             {control_kind::flow, "preview:" + std::string{actions[i]}}});
    }

    std::vector<std::string> rows;
    constexpr std::array states{"Idle", "Loading", "Published", "Rejected", "Failed"};
    rows.push_back("State: " +
                   std::string{states.at(static_cast<std::size_t>(preview_->status()))});
    const auto routes = preview_->routes();
    if (!routes.empty())
    {
        rows.push_back("Selected: " + routes[preview_route_ % routes.size()]);
    }

    rows.push_back("Route: " + std::string{preview_->active_route()});
    for (const auto& item : preview_->presentation())
    {
        rows.push_back(item.region + ": " + item.screen + (item.popup ? " [popup]" : ""));
        rows.push_back("Focus: " + item.focus + (item.input ? " | input" : " | suspended"));
    }

    for (const auto& [region, entries] : preview_->history())
    {
        std::string text = region + " history:";
        for (const auto& entry : entries)
        {
            text += " " + entry;
        }

        rows.push_back(std::move(text));
    }

    for (const auto& [edge, allowed] : preview_->conditions())
    {
        rows.push_back(edge + (allowed ? " allowed" : " blocked"));
    }

    rows.push_back("Traversal uses real guards");
    rows.push_back("Graph paths are structural");
    const auto count = static_cast<std::size_t>(std::max(1.F, bounds.height - 110) / 23);
    for (std::size_t i = 0; i < std::min(count, rows.size()); ++i)
    {
        plan.controls.push_back(
            {{bounds.x + bounds.width - 244, bounds.y + 54 + static_cast<float>(i) * 23, 240, 22},
             {0, 0, 0, 0},
             rows[i]});
    }

    plan.controls.push_back(
        {{bounds.x + 4, bounds.y + bounds.height - 26, std::max(1.F, bounds.width - 8), 23},
         {0, 0, 0, 0},
         status_.empty() ? std::string{preview_->diagnostic()} : status_});
}
} // namespace atlas::editor
