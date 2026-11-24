/** @file @brief Private owner-only tree transaction preserving live command-stream identity. */
#pragma once
#include "tree_state.hpp"

namespace atlas::ui::detail
{
/// @brief A distinct draft session prevents failed staging snapshots from aliasing live geometry.
class tree_edit
{
  public:
    explicit tree_edit(element_tree& tree); ///< Clone current state before admitting mutation.
    ~tree_edit();                         ///< Release the live-owner guard on failure and success.
    tree_edit(const tree_edit&) = delete; ///< One draft owns one publication.
    tree_edit& operator=(const tree_edit&) = delete; ///< Never share mutation authority.

    element_tree& draft() noexcept
    {
        return *draft_;
    } ///< Factory may edit only this private tree.

    element_handle staged(element_handle target) const; ///< Translate a live identity to the draft.
    element_handle published(element_handle target) const; ///< Translate back after validation.
    /// @brief Validate a complete sibling permutation before changing private intrusive links.
    void order_children(element_handle parent, std::span<const element_handle> children);
    void commit(); ///< No allocations after translating sparse identity-bearing references.
  private:
    void translate(element_tree& tree,
                   std::uint64_t session); ///< Rewrite private cross-node links.
    element_tree& tree_;                   ///< Original composition-owned session.
    std::unique_ptr<element_tree> draft_;  ///< Destroy after original owner is unguarded.
    bool committed_ = false;               ///< Prevent double publication.
};
} // namespace atlas::ui::detail
