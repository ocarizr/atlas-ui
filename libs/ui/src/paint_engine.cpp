/** @file @brief Owner-affine paint lifecycle and failure-atomic immutable packet publication. */
#include "paint_state.hpp"
#include <atlas/jobs/execution.hpp>

namespace atlas::ui
{
class paint_engine::impl
{
  public:
    impl(jobs::job_dispatcher client, paint_limits policy,
         std::shared_ptr<std::pmr::memory_resource> resource, diagnostics::services services)
        : dispatcher(std::move(client)), limits(policy), memory(std::move(resource)),
          destinations(std::move(services))
    {
    } ///< No pool, graphics objects or persistent packet allocations.

    void check() const
    {
        jobs::require_coordinator_thread();
        if (owner != std::this_thread::get_id() || busy)
        {
            throw std::logic_error("UI paint requires its non-reentrant owner coordinator");
        }
    } ///< Affinity is checked before observing mutable state.

    const std::thread::id owner = std::this_thread::get_id(); ///< Sole coordinator.
    jobs::job_dispatcher dispatcher;                   ///< Borrowed shared application service.
    paint_limits limits;                               ///< Validated bounded policy.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Shared allocation lifetime.
    diagnostics::services destinations;                ///< Optional logs and profiling.
    paint_statistics stats;                            ///< Reset before each calculation.
    bool busy = false;                                 ///< Reentrant calls are forbidden.
}; ///< Implementation details never enter native renderer contracts.

/// @brief Validate limits before allocating implementation state.
paint_engine::paint_engine(jobs::job_dispatcher dispatcher, paint_limits limits,
                           std::shared_ptr<std::pmr::memory_resource> memory,
                           diagnostics::services destinations)
{
    jobs::require_coordinator_thread();
    if (limits.max_nodes == 0 || limits.max_nodes > 65536 || limits.max_chunks == 0 ||
        limits.max_chunks > 64 || limits.min_parallel_quads == 0 ||
        limits.execution > paint_execution::chunks)
    {
        throw std::invalid_argument("invalid UI paint limits");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(std::move(dispatcher), limits, std::move(memory),
                                   std::move(destinations));
    impl_->busy = true;
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.paint",
                       "paint initialized");
    impl_->busy = false;
}

/// @brief Mutable services must not be released from workers or diagnostic callbacks.
paint_engine::~paint_engine()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.paint",
                       "paint released");
}

/// @brief No output escapes until preparation, every child, cancellation and allocation succeed.
paint_snapshot paint_engine::calculate(const element_snapshot& input,
                                       const layout_snapshot& geometry,
                                       const jobs::job_options& options)
{
    impl_->check();
    impl_->busy = true;

    /// @brief Restore the non-reentrant owner scope on every exit.
    struct reset
    {
        bool& value; ///< Guard reset before returning or throwing.

        ~reset()
        {
            value = false;
        } ///< Exception-safe owner restoration.
    } guard{impl_->busy};

    impl_->stats = {};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.paint.calculate", "ui.phase"}};
    try
    {
        detail::check_paint_cancel(options.cancellation);
        if (input.elements().size() > impl_->limits.max_nodes)
        {
            throw std::length_error("UI paint node budget exceeded");
        }

        impl_->stats.visited_nodes = input.elements().size();
        detail::paint_preparation prepared{impl_->memory.get()};
        {
            const diagnostics::operation_scope prepare{impl_->destinations,
                                                       {"ui.paint.order_clip", "ui.cpu"}};
            detail::prepare_paint(input, geometry, impl_->memory.get(), options.cancellation,
                                  prepared);
        }
        auto data = std::make_shared<detail::paint_data>(impl_->memory);
        data->session = input.session();
        data->revision = input.revision();
        data->viewport = input.viewport();
        data->values.resize(prepared.inputs.size());
        {
            const diagnostics::operation_scope numeric{impl_->destinations,
                                                       {"ui.paint.tessellate", "ui.cpu"}};
            detail::execute_paint(prepared, *data, impl_->dispatcher, impl_->limits, options,
                                  impl_->stats, impl_->destinations);
        }
        detail::check_paint_cancel(options.cancellation);
        impl_->stats.quads = data->values.size();
        paint_snapshot result;
        result.data_ = std::move(data);
        return result;
    }
    catch (...)
    {
        impl_->stats.failed = true;
        diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "ui.paint",
                           "paint preparation failed; no packet published");
        throw;
    }
}

/// @brief No unsynchronized access to the coordinator's statistics is supported.
paint_statistics paint_engine::statistics() const
{
    impl_->check();
    return impl_->stats;
}

/// @brief Default and moved-from snapshots have no source identity.
std::uint64_t paint_snapshot::session() const noexcept
{
    return data_ ? data_->session : 0;
}

/// @brief Geometry revision does not claim to version independent appearance edits.
std::uint64_t paint_snapshot::revision() const noexcept
{
    return data_ ? data_->revision : 0;
}

/// @brief Logical source extent accompanies projection and clip conversion.
layout_rect paint_snapshot::viewport() const noexcept
{
    return data_ ? data_->viewport : layout_rect{};
}

/// @brief Copies retain complete immutable storage without retaining the source tree.
std::span<const paint_quad> paint_snapshot::quads() const noexcept
{
    return data_ ? std::span<const paint_quad>{data_->values} : std::span<const paint_quad>{};
}
} // namespace atlas::ui
