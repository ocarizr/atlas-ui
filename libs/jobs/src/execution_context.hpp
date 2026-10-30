#pragma once

namespace atlas::jobs::detail
{
extern thread_local bool worker_thread;
struct task_state;
extern thread_local task_state* current_task;
void require_coordinator();
} // namespace atlas::jobs::detail
