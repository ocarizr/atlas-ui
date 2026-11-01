#include <atlas/jobs_diagnostics/observer.hpp>

#include <stdexcept>
#include <string>

namespace atlas::jobs_diagnostics
{
namespace
{
std::string_view status_name(jobs::job_status status) noexcept
{
    switch (status)
    {
        case jobs::job_status::succeeded:
            return "succeeded";
        case jobs::job_status::failed:
            return "failed";
        case jobs::job_status::cancelled:
            return "cancelled";
        case jobs::job_status::skipped:
            return "skipped";
        default:
            return "unknown";
    }
}
void log_completion(diagnostics::logger& log, const jobs::task_event& event) noexcept
{
    const auto result = status_name(event.status);
    const auto severity = event.status == jobs::job_status::failed ? diagnostics::log_level::error
                          : event.status == jobs::job_status::skipped
                              ? diagnostics::log_level::warning
                              : diagnostics::log_level::debug;
    if (!log.enabled(severity, "jobs"))
    {
        return;
    }
    try
    {
        std::string message = "job " + std::to_string(event.id) + " " + std::string{event.name} +
                              " " + std::string{result};
        if (event.exception)
        {
            try
            {
                std::rethrow_exception(event.exception);
            }
            catch (const std::exception& error)
            {
                message += ": ";
                message += error.what();
            }
            catch (...)
            {
                message += ": non-standard exception";
            }
        }
        (void)log.write(severity, "jobs", message);
    }
    catch (...)
    {
        (void)log.write(diagnostics::log_level::error, "jobs", "Job observation formatting failed");
    }
}
class observer final : public jobs::job_observer
{
  public:
    observer(std::shared_ptr<diagnostics::logger> log,
             std::shared_ptr<diagnostics::profiler> profile)
        : log_{std::move(log)}, profile_{std::move(profile)}
    {
    }
    void completed(const jobs::task_event& event) noexcept override
    {
        const auto result = status_name(event.status);
        diagnostics::event_context context;
        context.id = event.id;
        context.parent_id = event.parent_id;
        context.result = result;
        context.priority = static_cast<std::uint32_t>(event.priority);
        context.dependency_wait =
            std::chrono::duration_cast<std::chrono::nanoseconds>(event.queued - event.submitted);
        context.queue_wait =
            std::chrono::duration_cast<std::chrono::nanoseconds>(event.started - event.queued);
        context.join = std::chrono::duration_cast<std::chrono::nanoseconds>(event.completed -
                                                                            event.body_finished);
        context.total =
            std::chrono::duration_cast<std::chrono::nanoseconds>(event.completed - event.submitted);
        if (profile_)
        {
            const auto category =
                event.category.empty() ? std::string_view{"jobs"} : event.category;
            // Suppressed bodies have dispatch/cleanup intervals, not executed CPU work.
            if (event.body_executed)
            {
                (void)profile_->duration(event.name, category, event.started, event.body_finished,
                                         context, event.execution_thread);
            }
            else
            {
                (void)profile_->duration(event.name, "jobs.suppressed", event.started,
                                         event.started, context, event.execution_thread);
            }
            (void)profile_->instant(event.name, "jobs.completion", context);
        }
        if (log_)
        {
            log_completion(*log_, event);
        }
    }

  private:
    std::shared_ptr<diagnostics::logger> log_;
    std::shared_ptr<diagnostics::profiler> profile_;
};
} // namespace
std::shared_ptr<jobs::job_observer> make_observer(std::shared_ptr<diagnostics::logger> log,
                                                  std::shared_ptr<diagnostics::profiler> profile)
{
    if (!log && !profile)
    {
        throw std::invalid_argument{"A jobs observer needs a destination"};
    }
    return std::make_shared<observer>(std::move(log), std::move(profile));
}
} // namespace atlas::jobs_diagnostics
