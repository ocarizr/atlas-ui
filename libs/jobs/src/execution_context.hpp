#pragma once

namespace atlas::jobs::detail
{
extern thread_local bool worker_thread;
void require_coordinator();
} // namespace atlas::jobs::detail
