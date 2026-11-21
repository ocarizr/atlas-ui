/** @file @brief Separate atomic source/checkpoint writes and explicit recovery decisions. */
#include "owner_scope.hpp"
#include <atlas/editor_flow/persistence.hpp>

namespace atlas::editor_flow
{
namespace
{
/// @brief Every host operation is required before any document bytes are acquired.
void validate_storage(const std::filesystem::path& path, const storage& files)
{
    if (path.empty() || !files.read || !files.write || !files.remove)
    {
        throw std::invalid_argument(
            "Flow persistence requires a path and complete storage adapter");
    }
}
} // namespace

/// @brief Recovery is a complete XML document beside its source, never a partial edit journal.
std::filesystem::path recovery_path(const std::filesystem::path& source)
{
    auto path = source;
    path += ".atlas-flow-recovery.xml";
    return path;
}

/// @brief Opening never consumes recovery implicitly or overwrites a saved source.
persistence::persistence(std::filesystem::path path, storage files, bool restore,
                         diagnostics::services sinks)
    : path_{std::move(path)}, files_{std::move(files)}, sinks_{std::move(sinks)}
{
    validate_storage(path_, files_);
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.open", "editor.flow"}};
    const auto checkpoint = files_.read(recovery_path(path_));
    const auto bytes = restore ? checkpoint : files_.read(path_);
    if (!bytes)
    {
        throw std::invalid_argument("Flow source or requested recovery is missing");
    }

    edit_ = std::make_unique<session>(read_xml({*bytes}), !restore, 64, sinks_);
    pending_ = checkpoint.has_value() && !restore;
    checkpoint_ = restore ? edit_->revision() : 0;
    opened_ = edit_->revision();
}

/// @brief New graph creation does not truncate an existing source or abandoned recovery.
persistence::persistence(std::filesystem::path path, document initial, storage files,
                         diagnostics::services sinks, bool preserve_recovery)
    : path_{std::move(path)}, files_{std::move(files)}, sinks_{std::move(sinks)}
{
    validate_storage(path_, files_);
    if (files_.read(path_))
    {
        throw std::invalid_argument("Flow document or recovery already exists");
    }

    const auto checkpoint = files_.read(recovery_path(path_));
    if (checkpoint && !preserve_recovery)
    {
        throw std::invalid_argument("Flow document or recovery already exists");
    }

    if (checkpoint)
    {
        pending_ = true;
        try
        {
            initial.id = read_xml({*checkpoint}).id;
        }
        catch (const std::invalid_argument&)
        {
            diagnostics::write(sinks_, diagnostics::log_level::warning, "editor.flow",
                               "Malformed orphan recovery preserved for explicit discard");
        }
        catch (const std::length_error&)
        {
            diagnostics::write(sinks_, diagnostics::log_level::warning, "editor.flow",
                               "Oversized orphan recovery preserved for explicit discard");
        }
    }

    edit_ = std::make_unique<session>(std::move(initial), false, 64, sinks_);
    opened_ = edit_->revision();
    created_ = true;
}

/// @brief Persistence remains on the owner that selected the trusted target path.
void persistence::check() const
{
    if (closed_ || busy_ || owner_ != std::this_thread::get_id())
    {
        throw std::logic_error("Flow persistence requires its active owner");
    }
}

/// @brief Borrow the editor session until explicit close or persistence-owner destruction.
session& persistence::edit()
{
    check();
    return *edit_;
}

/// @brief Hosts can avoid checkpointing a never-used empty bootstrap without discarding recovery.
bool persistence::pristine_creation() const
{
    check();
    return created_ && !pending_ && edit_->revision() == opened_;
}

/// @brief A malformed checkpoint still remains pending until a restore or discard choice.
bool persistence::recovery_pending() const
{
    check();
    return pending_;
}

/// @brief Source replacement precedes cleanup; failed cleanup leaves the revision dirty for retry.
void persistence::save()
{
    check();
    const detail::owner_scope guard{busy_};
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.save", "editor.flow"}};
    const auto revision = edit_->revision();
    const auto bytes = write_xml(edit_->current());
    files_.write(path_, bytes);
    if (edit_->revision() != revision)
    {
        throw std::logic_error("Flow draft changed during save");
    }

    files_.remove(recovery_path(path_));
    if (edit_->revision() != revision)
    {
        throw std::logic_error("Flow draft changed during cleanup");
    }

    edit_->mark_saved();
    created_ = false;
    checkpoint_ = 0;
    pending_ = false;
}

/// @brief Never replace unresolved recovery with new edits from a later session.
void persistence::checkpoint()
{
    check();
    const detail::owner_scope guard{busy_};
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.checkpoint", "editor.flow"}};
    if (pending_)
    {
        if (edit_->dirty() && edit_->revision() != opened_)
        {
            throw std::logic_error("Resolve existing Flow recovery before checkpointing new edits");
        }

        return;
    }

    if (edit_->dirty() && edit_->revision() != checkpoint_)
    {
        const auto revision = edit_->revision();
        files_.write(recovery_path(path_), write_xml(edit_->current()));
        checkpoint_ = revision;
    }
    else if (!edit_->dirty() && checkpoint_)
    {
        files_.remove(recovery_path(path_));
        checkpoint_ = 0;
    }
}

/// @brief Recovery is an undoable authored edit and never a source-file write.
bool persistence::recover()
{
    check();
    const detail::owner_scope guard{busy_};
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.recover", "editor.flow"}};
    const auto revision = edit_->revision();
    const auto bytes = files_.read(recovery_path(path_));
    if (!bytes)
    {
        return false;
    }

    if (edit_->revision() != revision)
    {
        throw std::logic_error("Flow draft changed during recovery read");
    }

    edit_->apply(read_xml({*bytes}));
    checkpoint_ = edit_->revision();
    pending_ = false;
    return true;
}

/// @brief The host must explicitly select this destructive recovery-retirement operation.
void persistence::discard_recovery()
{
    check();
    const detail::owner_scope guard{busy_};
    files_.remove(recovery_path(path_));
    checkpoint_ = 0;
    pending_ = false;
}

/// @brief Failed preserve/discard leaves the session open and available for retry.
void persistence::close(bool discard)
{
    check();
    if (discard)
    {
        discard_recovery();
    }
    else
    {
        checkpoint();
    }

    closed_ = true;
    edit_.reset();
}
} // namespace atlas::editor_flow
