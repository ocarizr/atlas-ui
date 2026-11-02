/** @file @brief Scoped recording ranges with explicit payloads and terminal joins. */
#include <atlas/jobs/execution.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <vector>

namespace atlas::renderer_jobs
{
namespace
{
/// @brief Immutable pointer cells give declared-access tracking a single-level object range.
struct recording_work
{
    renderer::recording_task* task; ///< Borrowed through the joined range's lifetime.
};
} // namespace

/// @brief Preserve caller-selected priority/cancellation without owning scheduler lifetime.
executor::executor(jobs::job_dispatcher dispatcher) : dispatcher_(std::move(dispatcher))
{
}

/// @brief Use joined ranges; workers record only and the calling coordinator observes completion.
void executor::execute(std::span<renderer::recording_task* const> tasks)
{
    if (tasks.empty())
    {
        return;
    }

    jobs::require_coordinator_thread();

    for (const auto* task : tasks)
    {
        if (!task)
        {
            throw std::invalid_argument("recording tasks cannot be null");
        }
    }

    std::vector<recording_work> work;
    work.reserve(tasks.size());
    for (auto* task : tasks)
    {
        work.push_back({task});
    }

    auto completion = dispatcher_.parallel_for(
        jobs::job_label{"renderer recording", "renderer.cpu_record"},
        std::span<const recording_work>{work}, 1,
        [](jobs::job_context& context, std::span<const recording_work> batch)
        {
            for (const auto& item : batch)
            {
                context.throw_if_cancelled();
                item.task->run();
                context.throw_if_cancelled();
            }
        });
    completion.wait();
}
} // namespace atlas::renderer_jobs
