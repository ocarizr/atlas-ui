/** @file @brief Private staged handler replacement, retaining retired generations for cleanup. */
#pragma once
#include "input_state.hpp"

namespace atlas::ui::detail
{
/// @brief Additions and retirements publish only after the corresponding tree transaction succeeds.
class input_edit
{
  public:
    /// @brief Reject incompatible owner state without allocating staging metadata.
    static void require_idle(input_session& input, std::uint64_t session = 0);
    explicit input_edit(input_session& input); ///< Require an idle owner and copy current bindings.
    ~input_edit(); ///< Release the input owner guard after publication or failure.
    void retire(element_handle target); ///< Preserve handler until disable/end reconciliation.
    void manage_anchor(element_handle target); ///< Mark the privately owned viewport binding.
    void
    add(element_handle target, std::shared_ptr<input_handler> handler,
        interaction_options options); ///< Bound and validate admission before tree publication.
    void commit() noexcept;           ///< Nonallocating swap after all staging has succeeded.
  private:
    input_session& input_;                     ///< Composition-owned session.
    std::pmr::vector<input_binding> bindings_; ///< Temporary bounded complete registration table.
};
} // namespace atlas::ui::detail
