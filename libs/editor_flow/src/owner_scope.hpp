/** @file @brief Exception-safe owner reentrancy guard for host-provided callbacks. */
#pragma once

namespace atlas::editor_flow::detail
{
/// @brief Restrict arbitrary callback reentry while retaining ordinary exception unwinding.
class owner_scope
{
  public:
    explicit owner_scope(bool& busy) : busy_{busy}
    {
        busy_ = true;
    } ///< Claim a checked idle owner.

    ~owner_scope()
    {
        busy_ = false;
    } ///< Restore after success and failure.

    owner_scope(const owner_scope&) = delete;            ///< A scope has one owner.
    owner_scope& operator=(const owner_scope&) = delete; ///< Never share an active guard.
  private:
    bool& busy_; ///< Borrowed coordinator state.
};
} // namespace atlas::editor_flow::detail
