/** @file @brief Owner-thread layout lifecycle, cancellation and atomic live publication. */
#include "layout_state.hpp"
#include <atlas/jobs/execution.hpp>
#include <atlas/ui/element_tree.hpp>

namespace atlas::ui
{
namespace
{
/// @brief Root work is weighted by the engine's total dirty-node threshold before phase dispatch.
frame_limits phases(layout_limits limits)
{
    frame_limits result;
    result.max_items = limits.max_nodes;
    result.max_phases = 2;
    result.min_parallel_items = 1;
    result.grain = 1;
    result.max_chunks = limits.max_chunks;
    return result;
}

/// @brief Keep the engine guarded while scopes, callbacks and publication unwind.
class layout_guard
{
  public:
    explicit layout_guard(bool& busy) : busy_(busy)
    {
        busy_ = true;
    } ///< Enter checked scope.

    ~layout_guard()
    {
        busy_ = false;
    } ///< Restore even after allocation/cancellation failure.

    layout_guard(const layout_guard&) = delete;            ///< One reset owner.
    layout_guard& operator=(const layout_guard&) = delete; ///< Never duplicate guards.
  private:
    bool& busy_; ///< Borrowed owner-only flag.
};
} // namespace

/// @brief The phase executor and all branch vectors retain the same allocation policy.
layout_engine::impl::impl(jobs::job_dispatcher client, layout_limits policy,
                          std::shared_ptr<std::pmr::memory_resource> resource,
                          diagnostics::services services)
    : dispatcher(std::move(client)), limits(policy), memory(std::move(resource)),
      destinations(std::move(services)), frames(dispatcher, phases(policy), memory, destinations),
      cache(0, memory.get())
{
}

/// @brief Affinity is checked before mutable cache or busy-state observations.
void layout_engine::impl::check() const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy)
    {
        throw std::logic_error("UI layout requires its non-reentrant owner coordinator");
    }
}

/// @brief No scheduler, filesystem or graphics backend is created by layout.
layout_engine::layout_engine(jobs::job_dispatcher dispatcher, layout_limits limits,
                             std::shared_ptr<std::pmr::memory_resource> memory,
                             diagnostics::services destinations)
{
    jobs::require_coordinator_thread();
    if (limits.max_nodes == 0 || limits.max_nodes > 65536 || limits.min_parallel_nodes == 0 ||
        limits.max_chunks == 0 || limits.max_chunks > 64)
    {
        throw std::invalid_argument("invalid UI layout limits");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(std::move(dispatcher), limits, std::move(memory),
                                   std::move(destinations));
    const layout_guard guard{impl_->busy};
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.layout",
                       "layout initialized");
}

/// @brief Keep the guard active during nested phase/cache destruction and resource callbacks.
layout_engine::~layout_engine()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.layout",
                       "layout released");
}

/// @brief Immutable calculations may be retained independently from live publication.
layout_snapshot layout_engine::calculate(const element_snapshot& snapshot,
                                         const jobs::job_options& options)
{
    impl_->check();
    const layout_guard guard{impl_->busy};
    impl_->stats = {};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.layout.calculate", "ui.cpu"}};
    try
    {
        return layout_snapshot{impl_->compute(snapshot, options)};
    }
    catch (...)
    {
        impl_->stats.failed = true;
        throw;
    }
}

/// @brief Requested viewport changes remain requested inputs if calculation fails; no geometry
/// publishes.
layout_snapshot layout_engine::update(element_tree& tree, layout_rect viewport,
                                      const jobs::job_options& options)
{
    impl_->check();
    const layout_guard guard{impl_->busy};
    impl_->stats = {};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.layout.update", "ui.cpu"}};
    try
    {
        (void)impl_->dispatcher.with_options(options);
        if (options.cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        tree.set_viewport(viewport);
        const auto input = tree.snapshot();
        const layout_snapshot result{impl_->compute(input, options)};
        tree.publish_layout(result);
        return result;
    }
    catch (...)
    {
        impl_->stats.failed = true;
        throw;
    }
}

/// @brief Statistics are observational, not an automatic metrics/telemetry feed.
layout_statistics layout_engine::statistics() const
{
    impl_->check();
    return impl_->stats;
}
} // namespace atlas::ui
