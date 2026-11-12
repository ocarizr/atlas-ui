/** @file @brief Editor owner loop for transactional commands and cached panel presentation. */
#include "editor_view.hpp"
#include "document_file.hpp"
#include "panel_scene.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Keep authored values, persistent file and render handoff on their proper owners.
class editor_view::impl
{
  public:
    /// @brief Borrow the application job pool and preserve the selected document path.
    impl(document_session& active, jobs::job_system& workers, std::filesystem::path path,
         const diagnostics::services& destinations)
        : session{active}, jobs{workers}, file{std::move(path)}, sinks{destinations},
          layout{jobs.dispatcher(), {}, {}, sinks}, paint{jobs.dispatcher(), {}, {}, sinks}
    {
    }

    void execute(const control_action& action); ///< Run only outside Atlas event callbacks.
    void rebuild(ui::layout_rect viewport, float scale); ///< Publish complete chrome candidate.

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
};

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
    const auto plan = make_panels(session, {bounds.width, bounds.height}, page, status);
    chrome =
        std::make_unique<panel_scene>(plan, session.preview(), bounds, scale, layout, paint, sinks);
    viewport = bounds;
    density = scale;
}

/// @brief Initialize reusable layout/paint services on the main owner.
editor_view::editor_view(document_session& session, jobs::job_system& jobs,
                         std::filesystem::path file, const diagnostics::services& sinks)
    : impl_{std::make_unique<impl>(session, jobs, std::move(file), sinks)}
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

    if (!events.empty())
    {
        const auto actions = impl_->chrome->dispatch(events);
        for (const auto& action : actions)
        {
            impl_->execute(action);
        }

        if (!actions.empty())
        {
            impl_->rebuild(bounds, density);
        }
    }

    return impl_->chrome->packet();
}
} // namespace atlas::editor
