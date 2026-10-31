#pragma once

#include "priority_queue.hpp"

namespace atlas::jobs::detail
{
struct callback_mailbox
{
    priority_queue ready;
    ready_queue transfer;
    ready_queue executing;
    bool closed{false};
};
} // namespace atlas::jobs::detail
