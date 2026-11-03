/** @file @brief Bounded attached ranges with joined cleanup before publication or rethrow. */
#include "frame_state.hpp"
#include <atomic>

namespace atlas::ui
{
namespace
{
/// @brief Borrowed exclusively until the root's attached lifetime has completely joined.
struct phase_work
{
    detail::phase_task* task;     ///< Typed storage/kernel on the executing coordinator's stack.
    std::size_t count;            ///< Validated total element count.
    std::size_t grain;            ///< Coarse chunk length chosen within the admission cap.
    std::stop_token cancellation; ///< Same token used by the sequential path.
    const diagnostics::services* destinations; ///< Retained by the blocked coordinator.
    std::atomic<std::size_t> admitted{0}; ///< Actual accepted root/child count, not an estimate.
};

/// @brief Each payload names one exclusive index interval of the privately allocated output.
struct chunk_work
{
    phase_work* owner; ///< Borrowed through the attached parent join.
    std::size_t first; ///< Deterministic original offset.
    std::size_t count; ///< Positive bounded extent.
};

/// @brief A ready CPU body never accesses the live tree or waits on I/O/GPU work.
void run_chunk(jobs::job_context& context, chunk_work& chunk)
{
    const diagnostics::operation_scope scope{*chunk.owner->destinations,
                                             {"ui.phase.range", "ui.cpu"}};
    context.throw_if_cancelled();
    chunk.owner->task->run(chunk.first, chunk.count, chunk.owner->cancellation, &context);
    context.throw_if_cancelled();
}

/// @brief Partial child-admission failure still drains every previously attached child.
void run_root(jobs::job_context& context, phase_work*& work)
{
    for (std::size_t first = 0; first < work->count;)
    {
        context.throw_if_cancelled();
        const auto count = std::min(work->grain, work->count - first);
        (void)context.spawn(jobs::job_label{"ui range " + std::to_string(first), "ui.phase"},
                            chunk_work{work, first, count}, run_chunk);
        work->admitted.fetch_add(1, std::memory_order_relaxed);
        first += count;
    }
}
} // namespace

/// @brief Work results are never returned until all admitted payloads and declarations are retired.
void frame_executor::execute(std::string_view name, std::size_t count, std::size_t output_size,
                             detail::phase_task& task)
{
    impl_->validate(name);
    if (count > impl_->limits.max_items || count > impl_->limits.max_output_bytes / output_size)
    {
        throw std::overflow_error("UI phase input/output budget exhausted");
    }

    const auto identity = impl_->enter(name);
    const auto& saved = impl_->names.back();
    name = {saved.bytes.data(), saved.length};
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {name, "ui.phase"}};
    try
    {
        impl_->cancellation_point();
        task.prepare(impl_->memory, identity);
        impl_->cancellation_point();
        if (count == 0)
        {
            return;
        }

        if (count < impl_->limits.min_parallel_items || count <= impl_->limits.grain ||
            impl_->limits.max_chunks == 1)
        {
            task.run(0, count, impl_->options.cancellation, nullptr);
        }
        else
        {
            ++impl_->stats.parallel_phases;
            const auto grain = std::max(impl_->limits.grain,
                                        count / impl_->limits.max_chunks +
                                            (count % impl_->limits.max_chunks != 0 ? 1U : 0U));
            phase_work work{&task, count, grain, impl_->options.cancellation, &impl_->destinations};
            try
            {
                auto completion =
                    impl_->dispatcher.with_options(impl_->options)
                        .run(jobs::job_label{std::string{name}, "ui.phase"}, &work, run_root);
                work.admitted.fetch_add(1, std::memory_order_relaxed);
                completion.wait();
            }
            catch (const jobs::job_error& error)
            {
                impl_->stats.admitted_jobs += work.admitted.load(std::memory_order_relaxed);
                if (error.cause())
                {
                    std::rethrow_exception(error.cause());
                }

                throw;
            }
            catch (...)
            {
                impl_->stats.admitted_jobs += work.admitted.load(std::memory_order_relaxed);
                throw;
            }

            impl_->stats.admitted_jobs += work.admitted.load(std::memory_order_relaxed);
        }

        impl_->cancellation_point();
    }
    catch (...)
    {
        impl_->stats.failed = true;
        throw;
    }
}
} // namespace atlas::ui
