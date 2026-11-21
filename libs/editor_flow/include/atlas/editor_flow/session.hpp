/**
 * @file
 * @brief Bounded owner-thread graph history and immutable validated runtime candidates.
 */
#pragma once
#include <atlas/editor_flow/document.hpp>
#include <thread>

namespace atlas::editor_flow
{
/// @brief Optional catalog-snapshot check; no storage is opened by the authoring library.
using asset_check = std::function<bool(assets::asset_ref)>;

/// @brief Only the session can construct a compiled, generation-bound candidate.
class runtime_candidate
{
  public:
    [[nodiscard]] const ui_content::flow_document& graph() const; ///< Immutable runtime values.
    [[nodiscard]] std::string xml() const; ///< Canonical runtime XML, with no editor metadata.
    /// @brief Persistent predicates retained for explicit owner-domain resolution by the preview
    /// host.
    [[nodiscard]] const std::vector<transition_conditions>& conditions() const;

  private:
    friend class session;        ///< Validates before issuing a publication token.
    std::uint64_t owner_ = 0;    ///< Exact session, including independently opened documents.
    std::uint64_t revision_ = 0; ///< Exact authored revision at compilation.
    std::shared_ptr<const ui_content::flow_document> graph_; ///< Owned immutable runtime graph.
    asset_check assets_; ///< Retained snapshot capability, revalidated before publication.
    std::shared_ptr<const std::vector<transition_conditions>>
        conditions_; ///< Immutable decorators.
};

/// @brief Draft editing never mutates the last published runtime snapshot.
class session
{
  public:
    /// @brief Retain an independent document; bounded history counts retained revisions.
    explicit session(document source, bool saved = true, std::size_t history = 64,
                     diagnostics::services sinks = {});
    session(const session&) = delete;              ///< One mutable coordinator owner.
    session& operator=(const session&) = delete;   ///< Never copy history ownership.
    [[nodiscard]] const document& current() const; ///< Borrow until the next accepted edit.
    [[nodiscard]] std::uint64_t revision() const;  ///< Stable revision, restored by undo/redo.
    [[nodiscard]] bool dirty() const;              ///< True until this exact revision is saved.
    [[nodiscard]] bool can_undo() const;           ///< Retained earlier revision exists.
    [[nodiscard]] bool can_redo() const;           ///< Retained later revision exists.
    void apply(document source); ///< Publish a structurally valid draft as one undoable edit.
    bool undo();                 ///< Restore source, graph metadata and selection together.
    bool redo();                 ///< Restore an exact previously accepted revision.
    void mark_saved();           ///< Call only after durable source write and recovery cleanup.
    /// @brief Validate a private candidate against topology and optional immutable asset catalog.
    [[nodiscard]] runtime_candidate compile(const asset_check& assets = {}) const;
    /// @brief Reject foreign/stale candidates without changing the last valid runtime snapshot.
    [[nodiscard]] bool publish(const runtime_candidate& candidate);
    /// @brief Retain the last valid runtime independently from dirty drafts and their history.
    [[nodiscard]] std::shared_ptr<const runtime_candidate> published() const;

  private:
    /// @brief One immutable authored revision supports cheap history transfer.
    struct revision_state
    {
        std::shared_ptr<const document> source; ///< Never shared mutably with a caller.
        std::uint64_t id; ///< Monotonic allocation, never reused by divergent edits.
    };

    void check() const; ///< Reject access from foreign threads.
    void retain(std::vector<revision_state>& history, revision_state state); ///< Bounded history.
    revision_state current_;                             ///< Current authoring revision.
    std::vector<revision_state> undo_;                   ///< Reserved bounded earlier snapshots.
    std::vector<revision_state> redo_;                   ///< Reserved bounded later snapshots.
    std::shared_ptr<const runtime_candidate> published_; ///< Last accepted candidate.
    std::uint64_t saved_ = 0;                            ///< Identity of the last durable save.
    std::uint64_t next_ = 1;                             ///< Unique revision allocator.
    std::uint64_t identity_;                             ///< Unique session lifetime.
    std::size_t limit_;           ///< At most 128 retained history entries in each direction.
    diagnostics::services sinks_; ///< Optional logs/profiling supplied by the editor root.
    std::thread::id owner_ = std::this_thread::get_id(); ///< Immutable coordinator affinity.
    mutable bool busy_ = false; ///< Reject reentrant mutation from validation callbacks.
};
} // namespace atlas::editor_flow
