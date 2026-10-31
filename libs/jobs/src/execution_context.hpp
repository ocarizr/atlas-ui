#pragma once

#include <cstddef>

namespace atlas::jobs::detail
{
struct scheduler;
extern thread_local scheduler* current_scheduler;
extern thread_local std::size_t current_worker_index;
extern thread_local bool worker_thread;
extern thread_local bool observing_job;
struct task_state;
extern thread_local task_state* current_task;
void require_coordinator();
} // namespace atlas::jobs::detail
