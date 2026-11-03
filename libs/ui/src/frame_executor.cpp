/** @file @brief Frame lifecycle, validation and owner-thread serial dispatch. */
#include "frame_state.hpp"
#include <algorithm>

namespace atlas::ui
{
/// @brief No scheduler or worker thread is constructed here.
frame_executor::impl::impl(jobs::job_dispatcher client, frame_limits policy,
                           std::shared_ptr<std::pmr::memory_resource> resource,
                           diagnostics::services services)
    : dispatcher(std::move(client)), limits(policy), memory(std::move(resource)),
      destinations(std::move(services)), names(0, memory.get())
{
    names.reserve(limits.max_phases);
}

/// @brief Affinity precedes every mutable-state observation, including the sequential path.
void frame_executor::impl::check() const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy)
    {
        throw std::logic_error("UI frame requires its non-reentrant owner coordinator");
    }
}

/// @brief Failed or closed frames cannot silently admit later publication.
void frame_executor::impl::validate(std::string_view name) const
{
    check();
    if (stats.closed || stats.failed)
    {
        throw std::logic_error("UI phase requires an open successful frame");
    }

    if (name.empty() || name.size() > 64 || name.find_first_not_of(' ') == std::string_view::npos ||
        std::any_of(name.begin(), name.end(),
                    [](unsigned char value)
                    {
                        return value < 32 || value == 127;
                    }))
    {
        throw std::invalid_argument(
            "UI phase needs a nonempty name of at most 64 bytes without controls");
    }

    for (const auto& previous : names)
    {
        if (name == std::string_view{previous.bytes.data(), previous.length})
        {
            throw std::invalid_argument("UI phase name already used in this frame");
        }
    }

    if (names.size() == limits.max_phases)
    {
        throw std::overflow_error("UI frame phase capacity exhausted");
    }
}

/// @brief Admission follows complete preflight and cannot allocate after construction.
phase_identity frame_executor::impl::enter(std::string_view name)
{
    phase_name value;
    std::copy(name.begin(), name.end(), value.bytes.begin());
    value.length = name.size();
    names.push_back(value);
    ++stats.phases;
    return {stats.frame, stats.phases};
}

/// @brief Cancellation remains cooperative and identical for serial/parallel kernels.
void frame_executor::impl::cancellation_point() const
{
    if (options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }
}

/// @brief Limits reserve direct output/name capacity; nested user allocations are outside the
/// bound.
frame_executor::frame_executor(jobs::job_dispatcher dispatcher, frame_limits limits,
                               std::shared_ptr<std::pmr::memory_resource> memory,
                               diagnostics::services destinations)
{
    jobs::require_coordinator_thread();
    if (limits.max_items == 0 || limits.max_items > 1048576 || limits.max_output_bytes == 0 ||
        limits.max_phases == 0 || limits.max_phases > 64 || limits.max_chunks == 0 ||
        limits.max_chunks > 64 || limits.grain == 0 || limits.min_parallel_items == 0)
    {
        throw std::invalid_argument("UI frame limits exceed supported bounds");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(std::move(dispatcher), limits, std::move(memory),
                                   std::move(destinations));
    const impl::guard guard{impl_->busy};
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.frame",
                       "executor initialized");
}

/// @brief There is no asynchronous tail to drain; destruction during execution is forbidden.
frame_executor::~frame_executor()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    const impl::guard guard{impl_->busy};
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.frame",
                       "executor released");
}

/// @brief A newer frame can recover a failed frame, but cannot silently abandon an open success.
void frame_executor::begin_frame(std::uint64_t frame, jobs::job_options options)
{
    impl_->check();
    if (frame == 0 || frame <= impl_->stats.frame)
    {
        throw std::invalid_argument("UI frame identity must increase");
    }

    if (!impl_->stats.closed && !impl_->stats.failed)
    {
        throw std::logic_error("end the current UI frame first");
    }

    (void)impl_->dispatcher.with_options(options);
    impl_->names.clear();
    impl_->options = std::move(options);
    impl_->stats = {frame, 0, 0, 0, false, false};
}

/// @brief A sync boundary acknowledges only successful completion, not GPU submission/presentation.
void frame_executor::end_frame()
{
    impl_->check();
    if (impl_->stats.closed || impl_->stats.failed)
    {
        throw std::logic_error("cannot close this UI frame");
    }

    impl_->stats.closed = true;
}

/// @brief Read observations on the coordinator only.
frame_statistics frame_executor::statistics() const
{
    impl_->check();
    return impl_->stats;
}

/// @brief Serial callbacks may enqueue commands; their external side effects are not rolled back.
void frame_executor::serial(std::string_view name, void* payload, void (*function)(void*))
{
    impl_->validate(name);
    (void)impl_->enter(name);
    const auto& saved = impl_->names.back();
    name = {saved.bytes.data(), saved.length};
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {name, "ui.dispatch"}};
    try
    {
        impl_->cancellation_point();
        function(payload);
        impl_->cancellation_point();
    }
    catch (...)
    {
        impl_->stats.failed = true;
        throw;
    }
}
} // namespace atlas::ui
