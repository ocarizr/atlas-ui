/** @file @brief Owner-thread document sessions and independent recovery persistence. */
#pragma once
#include "document_session.hpp"
#include <functional>

namespace atlas::editor
{
/// @brief Explicit persistence operations, completed before returning to the workspace owner.
struct document_storage
{
    /// @brief Missing returns empty; malformed or inaccessible data throws.
    std::function<std::optional<ui_content::document>(const std::filesystem::path&)> read;
    /// @brief Atomically replace only after all authored bytes have been written.
    std::function<void(const std::filesystem::path&, const ui_content::document&)> write;
    /// @brief Remove an owned checkpoint; absence succeeds.
    std::function<void(const std::filesystem::path&)> remove;
    /// @brief Optional native path comparison after lexical normalization.
    std::function<bool(const std::filesystem::path&, const std::filesystem::path&)> same_path{};
    /// @brief Optional metadata-only existence query, including malformed checkpoints.
    std::function<bool(const std::filesystem::path&)> exists{};
};

/// @brief Explicit dirty-document retirement policy.
enum class close_policy : std::uint8_t
{
    preserve, ///< Keep dirty authoring in recovery before releasing the session.
    discard   ///< Explicitly remove the checkpoint before releasing the session.
};

/// @brief Derive a separate document-local recovery filename.
[[nodiscard]] std::filesystem::path recovery_path(const std::filesystem::path& source);

/// @brief Own up to 32 sessions, with no asynchronous callback borrowing their state.
class document_workspace
{
  public:
    /// @brief Retain composition services and require a complete persistence adapter.
    document_workspace(ui_content::construction options, document_storage storage);
    /// @brief Load a private candidate or activate the existing owner for the normalized path.
    [[nodiscard]] std::uint64_t open(std::filesystem::path path,
                                     std::shared_ptr<streaming::catalog_streamer> assets = {});
    /// @brief Create a fresh document only if neither an owner nor persistent data uses the path.
    [[nodiscard]] std::uint64_t create(std::filesystem::path path);
    /// @brief Restore a checkpoint even when the saved source is malformed.
    [[nodiscard]] std::uint64_t open_recovery(std::filesystem::path path);
    void activate(std::uint64_t id);          ///< Select a live stable identity.
    void cycle();                             ///< Select the next open document in insertion order.
    [[nodiscard]] document_session& active(); ///< Borrow until close or workspace destruction.
    [[nodiscard]] const std::filesystem::path& path() const; ///< Active persistent target.
    [[nodiscard]] std::uint64_t active_id() const noexcept;  ///< Zero when no sessions remain.
    [[nodiscard]] std::size_t size() const noexcept;         ///< Number of live document owners.
    [[nodiscard]] bool
    recovery_pending() const; ///< Preexisting checkpoint awaits an explicit choice.
    void save();   ///< Mark saved only after atomic replacement and recovery removal succeed.
    void reopen(); ///< Preserve unsaved edits in recovery before loading the source.
    [[nodiscard]] bool recover(); ///< Explicitly restore validated recovery as dirty authoring.
    void checkpoint_all(); ///< Persist every changed dirty revision, preserving earlier successes.
    void close(std::uint64_t id, close_policy policy); ///< Failure retains the selected session.
    /// @brief Rebind each preview to the current immutable source snapshot.
    void refresh_assets(const std::shared_ptr<streaming::catalog_streamer>& mount);
    bool references_asset(assets::asset_ref reference) const; ///< Check every live history.

  private:
    /// @brief One stable owner of authoring, preview and persistence state.
    struct entry
    {
        std::uint64_t id;                       ///< Never reused in this workspace.
        std::filesystem::path path;             ///< Normalized host-selected source filename.
        std::unique_ptr<document_session> view; ///< History and preview owned by this entry.
        std::uint64_t checkpoint = 0;           ///< Last revision successfully checkpointed.
        std::uint64_t opened = 0;               ///< Initial revision before local edits.
        bool recovery_pending = false;          ///< Protect an earlier owner's unsaved bytes.
    };

    entry& selected();             ///< Reject an empty workspace.
    const entry& selected() const; ///< Reject an empty workspace without mutation.
    /// @brief Build before publishing a newly allocated identity.
    std::uint64_t insert(std::filesystem::path path, ui_content::document source, bool saved,
                         std::shared_ptr<streaming::catalog_streamer> assets = {});
    void checkpoint(entry& item); ///< Successful persistence alone advances checkpoint state.
    /// @brief Respect backend-specific path identity.
    bool owns(const entry& item, const std::filesystem::path& path) const;
    bool exists(const std::filesystem::path& path) const; ///< Probe source or recovery presence.
    ui_content::construction options_;                    ///< Services outlive all document owners.
    document_storage storage_;                            ///< Synchronous persistence boundary.
    std::vector<entry> entries_;                          ///< Bounded, insertion-ordered ownership.
    std::uint64_t next_id_ = 1;                           ///< Monotonic document identity.
    std::uint64_t active_ = 0; ///< Selected identity independent of vector locations.
};
} // namespace atlas::editor
