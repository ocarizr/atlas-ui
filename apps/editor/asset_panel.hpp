/** @file @brief Type-oriented Atlas controls for one writable asset origin. */
#pragma once
#include "asset_files.hpp"
#include "document_workspace.hpp"
#include "panels.hpp"

namespace atlas::editor
{
/// @brief Asset authoring remains separate from document and tool contribution state.
class asset_panel
{
  public:
    /// @brief Borrow a repository that outlives the panel.
    asset_panel(asset_repository& repository, std::filesystem::path root);
    void append(panel_plan& plan); ///< Materialize bounded rows, actions and reference details.
    bool keyboard(std::span<const input::event> events); ///< ASCII search while active.
    /// @brief Handle an owner command after routing returns.
    bool execute(const control_action& action, document_workspace* documents,
                 jobs::job_system& jobs, const diagnostics::services& sinks);
    bool poll(document_workspace* documents, jobs::job_system& jobs,
              const diagnostics::services&
                  sinks); ///< Complete ready inspection and update visible status.
    void
    drop(assets::asset_id identity, std::string_view target, document_session& document,
         jobs::job_system& jobs,
         const diagnostics::services& sinks); ///< Copy a reference in one document transaction.
    bool visible = false;                     ///< Toggle browser against the document preview.
  private:
    assets::asset_ref selected() const; ///< Reject stale or absent selection.
    /// @brief Apply catalog history and deletion with document reference protection.
    bool mutate(control_kind kind, document_workspace* documents);
    void transfer(control_kind kind); ///< Resolve file selection and begin preparation.
    asset_repository& repository_;    ///< Durable metadata authority.
    std::filesystem::path root_;      ///< Authorized typed content origin.
    assets::asset_type type_ = assets::asset_type::ui; ///< Active category.
    std::optional<assets::asset_ref> selected_{};      ///< Stable selection.
    std::size_t page_ = 0;                             ///< Bounded list page.
    std::string query_;                                ///< ASCII path search.
    std::string status_;                               ///< Preparation status and failures.
};
} // namespace atlas::editor
