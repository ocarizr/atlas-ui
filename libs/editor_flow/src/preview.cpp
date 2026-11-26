/** @file @brief Explicit preview application, bounded preflight and last-valid-state preservation.
 */
#include "owner_scope.hpp"
#include "preview_state.hpp"
#include <algorithm>

namespace atlas::editor_flow
{
/// @brief Engines retain the project allocator; no independent scheduler or native file access.
preview::impl::impl(const jobs::job_dispatcher& jobs,
                    std::shared_ptr<std::pmr::memory_resource> memory,
                    diagnostics::services destinations)
    : layout{jobs, {}, memory, destinations}, paint{jobs, {}, std::move(memory), destinations},
      sinks{std::move(destinations)}
{
}

/// @brief Refuse foreign-thread state inspection and dispatch, including cheap status queries.
void preview::impl::check() const
{
    if (owner != std::this_thread::get_id() || busy)
    {
        throw std::logic_error("Flow preview requires its owner thread outside dispatch");
    }
}

/// @brief Construct an empty isolated editor preview with borrowed application services.
preview::preview(const jobs::job_dispatcher& jobs,
                 std::shared_ptr<std::pmr::memory_resource> memory, diagnostics::services sinks)
    : impl_{std::make_unique<impl>(jobs, std::move(memory), std::move(sinks))}
{
}

/// @brief Best-effort lifecycle cleanup cannot escape destruction.
preview::~preview()
{
    try
    {
        stop();
    }
    catch (...)
    {
        diagnostics::write(impl_->sinks, diagnostics::log_level::warning, "editor.flow.preview",
                           "Preview destruction could not complete owner lifecycle cleanup");
    }
}

/// @brief Drop borrowed viewport metadata before retiring runtime views and their domains.
void preview::impl::close(std::shared_ptr<runtime>& state)
{
    if (!state)
    {
        return;
    }

    state->viewport.reset();
    if (state->flow)
    {
        try
        {
            state->suppress_actions = true;
            state->flow->shutdown(++clock);
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.flow.preview",
                               error.what());
        }
    }

    state.reset();
}

/// @brief Build a replacement privately; no implicit application follows document mutations.
void preview::apply(runtime_candidate candidate, ui_content::flow_loader loader,
                    preview_registration registration)
{
    impl_->check();
    const detail::owner_scope guard{impl_->busy};
    (void)candidate.graph();
    if (!loader)
    {
        throw std::invalid_argument("Flow preview requires a screen loader");
    }

    impl_->probe.reset();
    impl_->close(impl_->pending);
    auto next = std::make_shared<impl::runtime>();
    next->candidate = std::move(candidate);
    next->loader = std::move(loader);
    next->registration = std::move(registration);
    next->sinks = impl_->sinks;
    impl_->pending = std::move(next);
    impl_->cursor = 0;
    impl_->diagnostic = "Validating preview screens";
}

/// @brief Cancellation invalidates private loads and pending navigation, preserving active views.
void preview::cancel()
{
    impl_->check();
    const detail::owner_scope guard{impl_->busy};
    impl_->abandon();
    impl_->diagnostic = "Preview candidate cancelled";
}

/// @brief Internal cancellation lets stop clean up without reentering a public operation.
void preview::impl::abandon()
{
    probe.reset();
    close(pending);
    if (active)
    {
        active->flow->cancel();
        active->navigation.reset();
    }
}

/// @brief Restart uses the immutable active snapshot, never the dirty editor draft.
void preview::restart()
{
    impl_->check();
    if (!impl_->active)
    {
        throw std::logic_error("Restart requires an applied preview");
    }

    apply(impl_->active->candidate, impl_->active->loader, impl_->active->registration);
}

/// @brief Stop is independent from XML source persistence and editor undo history.
void preview::stop()
{
    impl_->check();
    const detail::owner_scope guard{impl_->busy};
    impl_->abandon();
    impl_->close(impl_->active);
    impl_->diagnostic = "Preview stopped";
    impl_->status = ui_content::flow_status::idle;
}

/// @brief Validate every screen before constructing a separate manager with the same runtime.
void preview::impl::poll(ui::layout_rect bounds)
{
    if (!pending)
    {
        return;
    }

    const diagnostics::operation_scope scope{sinks, {"editor.flow.preview.apply", "editor.flow"}};
    const auto& graph = pending->candidate.graph();
    if (cursor < graph.screens.size())
    {
        if (!probe)
        {
            const auto& screen = graph.screens[cursor];
            ui_content::flow_load_request request{screen.asset, {}, {}, screen.id};
            for (const auto& edge : graph.transitions)
            {
                if (edge.from == screen.id && !edge.control.empty())
                {
                    request.controls.push_back({edge.control, edge.id, edge.action});
                }
            }

            request.dispatch = [](auto)
            {
                return false;
            };
            probe = pending->factory()(std::move(request));
        }

        if (!probe)
        {
            throw std::runtime_error("Flow preview loader returned no load");
        }

        if (auto view = probe->poll())
        {
            pending->guards.erase(view.get());
            pending->actions.erase(view.get());
            probe.reset();
            ++cursor;
        }

        return;
    }

    if (!pending->flow)
    {
        auto* state = pending.get();
        pending->flow = std::make_unique<ui_content::flow_manager>(graph, pending->factory(), sinks,
                                                                   [state](const auto& edge)
                                                                   {
                                                                       return state->guard(edge);
                                                                   });
        pending->flow->start();
    }

    const auto result = pending->flow->advance(++clock);
    if (result == ui_content::flow_status::failed)
    {
        std::rethrow_exception(pending->flow->failure());
    }

    if (result == ui_content::flow_status::committed)
    {
        auto viewport =
            std::make_unique<ui_content::flow_viewport>(*pending->flow, layout, paint, sinks);
        for (const auto& item : pending->flow->presentation())
        {
            pending->live.emplace(item.screen, item.view);
            if (item.input)
            {
                pending->eligible.emplace(item.screen);
            }
        }

        (void)viewport->frame(++clock, bounds);
        close(active);
        pending->viewport = std::move(viewport);
        active = std::move(pending);
        diagnostic = "Preview applied";
    }
}

/// @brief Sync before serial Lua/input; only immutable layout/paint work uses shared jobs.
std::vector<ui::paint_quad> preview::frame(ui::layout_rect bounds,
                                           std::span<const ui::input_event> events,
                                           const ui_content::flow_frame_options& options)
{
    impl_->check();
    const detail::owner_scope guard{impl_->busy};
    ui::validate_input_events(events);
    if (events.size() > 256)
    {
        throw std::length_error("Flow preview input budget exhausted");
    }

    try
    {
        impl_->poll(bounds);
    }
    catch (const std::exception& error)
    {
        impl_->diagnostic = error.what();
        diagnostics::write(impl_->sinks, diagnostics::log_level::warning, "editor.flow.preview",
                           error.what());
        impl_->probe.reset();
        impl_->close(impl_->pending);
    }

    if (!impl_->active)
    {
        return {};
    }

    impl_->step();
    auto& active = *impl_->active;
    try
    {
        return active.viewport->frame(++impl_->clock, bounds, events, options);
    }
    catch (...)
    {
        ++impl_->clock;
        throw;
    }
}

/// @brief Synchronize navigation and compact generation-bound cache entries after publication.
void preview::impl::step()
{
    auto& runtime_state = *active;
    if (auto action = std::exchange(runtime_state.navigation, {}))
    {
        if (action->action == "atlas.navigate_to")
        {
            runtime_state.route = action->route;
            (void)runtime_state.flow->request(action->route);
        }
        else if (action->action == "atlas.exit_menu")
        {
            (void)runtime_state.flow->exit_menu(action->screen);
        }
        else
        {
            (void)runtime_state.flow->close_popup(action->screen);
        }
    }

    {
        const detail::owner_scope retirement{runtime_state.suppress_actions};
        status = runtime_state.flow->advance(++clock);
    }
    if (status == ui_content::flow_status::failed)
    {
        try
        {
            std::rethrow_exception(runtime_state.flow->failure());
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
        }
    }
    else if (status == ui_content::flow_status::rejected)
    {
        diagnostic = "Navigation rejected; active presentation preserved";
    }

    const auto visible = runtime_state.flow->presentation();
    runtime_state.live.clear();
    runtime_state.eligible.clear();
    for (const auto& item : visible)
    {
        runtime_state.live.emplace(item.screen, item.view);
        if (item.input)
        {
            runtime_state.eligible.emplace(item.screen);
        }
    }

    if (status != ui_content::flow_status::loading)
    {
        std::erase_if(runtime_state.guards,
                      [&](const auto& value)
                      {
                          return std::ranges::none_of(visible,
                                                      [&](const auto& item)
                                                      {
                                                          return item.view == value.first;
                                                      });
                      });
        std::erase_if(runtime_state.actions,
                      [&](const auto& value)
                      {
                          return std::ranges::none_of(visible,
                                                      [&](const auto& item)
                                                      {
                                                          return item.view == value.first;
                                                      });
                      });
    }
}

/// @brief Explicit route requests and event replay remain different testing operations.
bool preview::request(std::string_view route)
{
    impl_->check();
    if (!impl_->active)
    {
        return false;
    }

    impl_->active->route = route;
    return impl_->active->flow->request(route);
}
} // namespace atlas::editor_flow
