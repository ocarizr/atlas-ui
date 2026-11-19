/** @file @brief Document commands and timed checkpointing outside UI dispatch. */
#include "document_file.hpp"
#include "editor_view_state.hpp"

namespace atlas::editor
{
/// @brief Preserve the original document when starting another authoring session.
void editor_view::impl::new_session()
{
    if (!workspace)
    {
        session().replace(new_document(), false);
        return;
    }

    const auto candidate =
        path().parent_path() / ("untitled-" + std::to_string(next_document++) + ".xml");
    (void)workspace->create(candidate);
}

/// @brief Apply document ownership and persistence separately from element editing.
bool editor_view::impl::execute_document(control_kind kind)
{
    if (!workspace &&
        (kind == control_kind::cycle_document || kind == control_kind::close_document ||
         kind == control_kind::recover_document))
    {
        throw std::logic_error("This editor view has no document workspace");
    }

    switch (kind)
    {
        case control_kind::new_document:
            new_session();
            page = 0;
            break;
        case control_kind::save:
            if (workspace)
            {
                workspace->save();
            }
            else
            {
                save_document(path(), session().document(), sinks);
                session().mark_saved();
            }
            status = "Document saved";
            break;
        case control_kind::reopen:
            if (workspace)
            {
                workspace->reopen();
            }
            else
            {
                session().replace(open_document(path(), jobs, sinks), true);
            }
            page = 0;
            status = "Document reopened";
            break;
        case control_kind::cycle_document:
            workspace->cycle();
            page = 0;
            break;
        case control_kind::recover_document:
            status = workspace->recover() ? "Recovery restored; save to accept" : "No recovery";
            page = 0;
            break;
        case control_kind::close_document:
            if (workspace->size() > 1)
            {
                workspace->close(workspace->active_id(), close_policy::preserve);
                page = 0;
            }
            else
            {
                status = "Keep one document open; close the window to exit";
            }
            break;
        default:
            return false;
    }

    return true;
}

/// @brief Dispatch editor operations and preserve active state when persistence or editing fails.
void editor_view::impl::execute(const control_action& action)
{
    const diagnostics::operation_scope scope{sinks, {"editor.command", "editor"}};
    try
    {
        status.clear();
        if (execute_document(action.kind))
        {
            return;
        }

        switch (action.kind)
        {
            case control_kind::select:
                session().select(action.target);
                break;
            case control_kind::add_child:
                if (session().apply(edit_kind::add_child))
                {
                    page = (session().document().elements.size() - 1) / hierarchy_page_size;
                }
                break;
            case control_kind::remove:
                (void)session().apply(edit_kind::remove);
                break;
            case control_kind::narrower:
                (void)session().apply(edit_kind::narrower);
                break;
            case control_kind::wider:
                (void)session().apply(edit_kind::wider);
                break;
            case control_kind::toggle_visible:
                (void)session().apply(edit_kind::toggle_visible);
                break;
            case control_kind::undo:
                (void)session().undo();
                break;
            case control_kind::redo:
                (void)session().redo();
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
            default:
                throw std::invalid_argument("Unknown editor command");
        }
    }
    catch (const std::exception& error)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor", error.what());
        status = "Operation failed. See log.";
    }
}

/// @brief A failed final checkpoint retains every session and exposes an actionable status.
bool editor_view::preserve()
{
    try
    {
        if (impl_->workspace)
        {
            impl_->workspace->checkpoint_all();
        }

        return true;
    }
    catch (const std::exception& error)
    {
        diagnostics::write(impl_->sinks, diagnostics::log_level::error, "editor.recovery",
                           error.what());
        impl_->status = "Recovery write failed. Save or fix storage before closing.";
        impl_->chrome.reset();
        return false;
    }
}

/// @brief Periodic synchronous checkpointing cannot race document retirement.
void editor_view::autosave(std::chrono::steady_clock::time_point now)
{
    if (now >= impl_->next_autosave)
    {
        impl_->next_autosave = now + std::chrono::seconds{30};
        (void)preserve();
    }
}

} // namespace atlas::editor
