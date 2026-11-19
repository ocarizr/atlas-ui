/** @file @brief Transactional workspace lifecycle and bounded recovery checkpoints. */
#include "document_workspace.hpp"
#include <algorithm>
#include <cwctype>
#include <limits>

namespace atlas::editor
{
namespace
{
/// @brief Normalize source identity and reserve recovery filenames across backend case policies.
std::filesystem::path normalized_source(std::filesystem::path path)
{
    if (path.empty())
    {
        throw std::invalid_argument("Editor document path is empty");
    }

    path = path.lexically_normal();
    auto filename = path.filename().wstring();
    std::ranges::transform(filename, filename.begin(),
                           [](wchar_t value)
                           {
                               return static_cast<wchar_t>(std::towlower(value));
                           });
    if (filename.ends_with(L".atlas-recovery.xml"))
    {
        throw std::invalid_argument("Recovery paths are reserved; use explicit recovery");
    }

    return path;
}
} // namespace

/// @brief Append an explicit extension so ordinary UI source remains separately owned.
std::filesystem::path recovery_path(const std::filesystem::path& source)
{
    auto result = source;
    result += ".atlas-recovery.xml";
    return result;
}

/// @brief Each workspace owns its service callbacks and every mutable session.
document_workspace::document_workspace(ui_content::construction options, document_storage storage)
    : options_{std::move(options)}, storage_{std::move(storage)}
{
    if (!storage_.read || !storage_.write || !storage_.remove)
    {
        throw std::invalid_argument("Document storage requires read, write and remove");
    }

    entries_.reserve(32);
}

/// @brief Resolve by durable identity, never by a borrowed vector index.
const document_workspace::entry& document_workspace::selected() const
{
    const auto found = std::ranges::find(entries_, active_, &entry::id);
    if (found == entries_.end())
    {
        throw std::logic_error("No active editor document");
    }

    return *found;
}

/// @brief Share the same identity check for mutable owner access.
document_workspace::entry& document_workspace::selected()
{
    return const_cast<entry&>(std::as_const(*this).selected());
}

/// @brief Construction failure leaves the old active session intact.
std::uint64_t document_workspace::insert(std::filesystem::path path, ui_content::document source,
                                         bool saved)
{
    if (entries_.size() == 32 || next_id_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Editor workspace capacity exhausted");
    }

    auto view = std::make_unique<document_session>(std::move(source), options_);
    if (saved)
    {
        view->mark_saved();
    }

    const auto id = next_id_;
    const auto opened = view->revision_id();
    entries_.push_back({id, std::move(path), std::move(view), 0, opened});
    ++next_id_;
    active_ = id;
    return id;
}

/// @brief Existing entries retain history when opened again.
std::uint64_t document_workspace::open(std::filesystem::path path)
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"editor.document.open", "editor"}};
    path = normalized_source(std::move(path));
    const auto found = std::ranges::find_if(entries_,
                                            [&](const auto& item)
                                            {
                                                return owns(item, path);
                                            });
    if (found != entries_.end())
    {
        active_ = found->id;
        return active_;
    }

    auto source = storage_.read(path);
    const bool saved = source.has_value();
    const bool pending = exists(recovery_path(path));
    const auto id = insert(std::move(path), source ? std::move(*source) : new_document(), saved);
    selected().recovery_pending = pending;
    return id;
}

/// @brief New-document actions never replace another document's edits.
std::uint64_t document_workspace::create(std::filesystem::path path)
{
    path = normalized_source(std::move(path));
    if (std::ranges::any_of(entries_,
                            [&](const auto& item)
                            {
                                return owns(item, path);
                            }) ||
        exists(path) || exists(recovery_path(path)))
    {
        throw std::invalid_argument("Editor document path is already in use");
    }

    return insert(std::move(path), new_document(), false);
}

/// @brief Native adapters can compare case and normalized absolute paths consistently.
bool document_workspace::owns(const entry& item, const std::filesystem::path& path) const
{
    return storage_.same_path ? storage_.same_path(item.path, path) : item.path == path;
}

/// @brief Backends can probe malformed data without parsing it as a normal document.
bool document_workspace::exists(const std::filesystem::path& path) const
{
    if (storage_.exists)
    {
        return storage_.exists(path);
    }

    try
    {
        return storage_.read(path).has_value();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
}

/// @brief Explicit recovery bypasses malformed saved bytes and validates before publication.
std::uint64_t document_workspace::open_recovery(std::filesystem::path path)
{
    path = normalized_source(std::move(path));
    if (std::ranges::any_of(entries_,
                            [&](const auto& item)
                            {
                                return owns(item, path);
                            }))
    {
        throw std::invalid_argument("Document is already open; use Recover");
    }

    auto source = storage_.read(recovery_path(path));
    if (!source)
    {
        throw std::invalid_argument("No recovery checkpoint for editor document");
    }

    const auto id = insert(std::move(path), std::move(*source), false);
    selected().checkpoint = selected().view->revision_id();
    return id;
}

/// @brief Reject closed identities without changing the current selection.
void document_workspace::activate(std::uint64_t id)
{
    if (std::ranges::find(entries_, id, &entry::id) == entries_.end())
    {
        throw std::invalid_argument("Unknown editor document");
    }

    active_ = id;
}

/// @brief Rotate stable entries without rebuilding their previews.
void document_workspace::cycle()
{
    const auto found = std::ranges::find(entries_, active_, &entry::id);
    if (found == entries_.end())
    {
        throw std::logic_error("No active editor document");
    }

    const auto next = std::next(found);
    active_ = next == entries_.end() ? entries_.front().id : next->id;
}

/// @brief Borrow only from the live owner selected by identity.
document_session& document_workspace::active()
{
    return *selected().view;
}

/// @brief Persistent target belongs to its entry, independently of selection or preview handles.
const std::filesystem::path& document_workspace::path() const
{
    return selected().path;
}

/// @brief Zero identifies an empty workspace.
std::uint64_t document_workspace::active_id() const noexcept
{
    return active_;
}

/// @brief Expose the bounded live owner count.
std::size_t document_workspace::size() const noexcept
{
    return entries_.size();
}

/// @brief Expose a prior checkpoint without consuming or replacing its content.
bool document_workspace::recovery_pending() const
{
    return selected().recovery_pending;
}

/// @brief Complete preservation before releasing authoring, history and runtime state.
void document_workspace::close(std::uint64_t id, close_policy policy)
{
    const auto found = std::ranges::find(entries_, id, &entry::id);
    if (found == entries_.end())
    {
        throw std::invalid_argument("Unknown editor document");
    }

    if (policy == close_policy::preserve)
    {
        checkpoint(*found);
    }
    else if (policy == close_policy::discard)
    {
        storage_.remove(recovery_path(found->path));
    }
    else
    {
        throw std::invalid_argument("Unknown editor close policy");
    }

    entries_.erase(found);
    if (active_ == id)
    {
        active_ = entries_.empty() ? 0 : entries_.front().id;
    }
}
} // namespace atlas::editor
