/**
 * @file
 * @brief Unique thread ownership and mailbox retirement on owner exit.
 */

#pragma once

#include "priority_queue.hpp"

namespace atlas::jobs::detail
{
/// @brief Origin-thread notification queue with explicit owner-exit and affine delivery state.
struct callback_mailbox
{
    /// @brief Owned weighted ready queues for notifications bound to this origin.
    priority_queue ready;
    /// @brief Prepared ready entries temporarily detached during mailbox retirement.
    ready_queue transfer;
    /// @brief Prepared entries retained while origin-affine callbacks execute.
    ready_queue executing;
    /// @brief Whether the origin mailbox can no longer accept or deliver notifications.
    bool closed{false};
};
} // namespace atlas::jobs::detail
