/**
 * @file
 * @brief Atomic host persistence and explicit recovery choices for one graph document.
 */
#pragma once
#include <atlas/editor_flow/session.hpp>
#include <filesystem>

namespace atlas::editor_flow
{
/// @brief Host-authorized paths; reads use the streamer and writes replace complete files
/// atomically.
struct storage
{
    std::function<std::optional<std::string>(const std::filesystem::path&)>
        read; ///< Missing returns empty.
    std::function<void(const std::filesystem::path&, std::string_view)>
        write; ///< All-or-old replacement.
    std::function<void(const std::filesystem::path&)>
        remove; ///< Absence succeeds; other failures throw.
};

/// @brief Keep recovery separate from source and from ordinary UI-document recovery files.
[[nodiscard]] std::filesystem::path recovery_path(const std::filesystem::path& source);

/// @brief Own one persistent graph; the editor workspace can retain multiple independent owners.
class persistence
{
  public:
    /// @brief Open source without consuming a preexisting checkpoint; restore bypasses malformed
    /// source.
    persistence(std::filesystem::path path, storage files, bool restore = false,
                diagnostics::services sinks = {});
    /// @brief Create an unsaved owner. Explicit protected-open mode retains an orphan checkpoint
    /// for a restore/discard decision, without restoring its graph implicitly.
    persistence(std::filesystem::path path, document initial, storage files,
                diagnostics::services sinks = {}, bool preserve_recovery = false);
    [[nodiscard]] session& edit(); ///< Mutable authoring remains on the opening thread.
    /// @brief An unchanged newly created revision with no unresolved recovery decision.
    [[nodiscard]] bool pristine_creation() const;
    [[nodiscard]] bool
    recovery_pending() const; ///< Existing checkpoint awaits an explicit decision.
    void save();       ///< Save even incomplete drafts; mark clean only after complete cleanup.
    void checkpoint(); ///< Preserve pending old recovery and skip an unchanged authored revision.
    [[nodiscard]] bool recover(); ///< Restore as an undoable dirty edit; source remains untouched.
    void discard_recovery();  ///< Explicitly remove old recovery before starting fresh checkpoints.
    void close(bool discard); ///< Preserve unsaved edits, or explicitly retire their checkpoint.
  private:
    void check() const;             ///< Enforce owner and reject operations after successful close.
    std::filesystem::path path_;    ///< Trusted host-selected document path.
    storage files_;                 ///< Owned persistence callbacks outlive session operations.
    diagnostics::services sinks_;   ///< Optional structured diagnostics.
    std::unique_ptr<session> edit_; ///< One active history owner.
    std::uint64_t checkpoint_ = 0;  ///< Last completely written recovery revision.
    std::uint64_t opened_ = 0; ///< Initial revision may coexist with unresolved orphan recovery.
    bool pending_ = false;     ///< Never silently overwrite an existing recovery draft.
    bool busy_ = false;        ///< Reject persistence reentry from storage callbacks.
    bool closed_ = false;      ///< Set only after successful preserve/discard.
    bool created_ = false;     ///< A new unsaved source, rather than an opened/restored document.
    std::thread::id owner_ = std::this_thread::get_id(); ///< Persistence stays on its coordinator.
};
} // namespace atlas::editor_flow
