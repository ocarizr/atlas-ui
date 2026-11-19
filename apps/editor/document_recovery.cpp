/** @file @brief Atomic source saves and recovery checkpoint ownership. */
#include "document_workspace.hpp"

namespace atlas::editor
{
/// @brief Keep dirty state on any persistence or checkpoint-cleanup failure.
void document_workspace::save()
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"editor.document.save", "editor"}};
    auto& item = selected();
    storage_.write(item.path, item.view->document());
    storage_.remove(recovery_path(item.path));
    item.view->mark_saved();
    item.checkpoint = 0;
    item.recovery_pending = false;
}

/// @brief Capture edits before destructive reload, then publish only a valid source.
void document_workspace::reopen()
{
    auto& item = selected();
    checkpoint(item);
    auto source = storage_.read(item.path);
    if (!source)
    {
        throw std::invalid_argument("Editor document is missing");
    }

    item.view->replace(std::move(*source), true);
}

/// @brief Recovery leaves the source untouched and retains a fresh dirty authoring revision.
bool document_workspace::recover()
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"editor.document.recover", "editor"}};
    auto& item = selected();
    auto source = storage_.read(recovery_path(item.path));
    if (!source)
    {
        return false;
    }

    item.view->restore(std::move(*source));
    item.checkpoint = item.view->revision_id();
    item.recovery_pending = false;
    return true;
}

/// @brief Checkpoint identities avoid rewriting unchanged edits on each autosave interval.
void document_workspace::checkpoint(entry& item)
{
    if (item.recovery_pending)
    {
        if (item.view->dirty() && item.view->revision_id() != item.opened)
        {
            throw std::logic_error("Recovery already exists. Recover it or Save before closing.");
        }

        return;
    }

    if (item.view->dirty() && item.checkpoint != item.view->revision_id())
    {
        storage_.write(recovery_path(item.path), item.view->document());
        item.checkpoint = item.view->revision_id();
    }
    else if (!item.view->dirty() && item.checkpoint != 0)
    {
        storage_.remove(recovery_path(item.path));
        item.checkpoint = 0;
    }
}

/// @brief A partial failure preserves sessions and every previously published checkpoint.
void document_workspace::checkpoint_all()
{
    const diagnostics::operation_scope scope{options_.diagnostics, {"editor.autosave", "editor"}};
    std::exception_ptr failure;
    for (auto& item : entries_)
    {
        try
        {
            checkpoint(item);
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
    }

    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

} // namespace atlas::editor
