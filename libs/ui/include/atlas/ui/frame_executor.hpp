/**
 * @file
 * @brief Owner-thread named phases with bounded joined jobs and a sequential small-work path.
 */
#pragma once
#include <atlas/diagnostics/instrumentation.hpp>
#include <atlas/ui/detail/phase_work.hpp>
#include <string_view>

namespace atlas::ui
{
/// @brief Explicit admission limits, not a global scheduler or process-memory bound.
struct frame_limits
{
    std::size_t max_items = 65536; ///< At most 1048576 inputs/outputs per work phase.
    std::size_t max_output_bytes =
        std::size_t{64} * 1024 * 1024; ///< Direct output storage, excluding nested allocations.
    std::uint32_t max_phases = 32;     ///< At most 64 distinct serial/work phase names per frame.
    std::size_t min_parallel_items =
        4096;                      ///< Conservative small-work guard; tune per measured kernel.
    std::size_t grain = 128;       ///< Minimum elements per coarse chunk; never zero.
    std::uint32_t max_chunks = 16; ///< At most 64 attached children plus one root per phase.
};

/// @brief Owner-thread observations; attempted phases include a failed phase.
struct frame_statistics
{
    std::uint64_t frame = 0;           ///< Current or most recently closed frame.
    std::uint32_t phases = 0;          ///< Names admitted in stable call order.
    std::uint32_t parallel_phases = 0; ///< Work phases attempting worker admission.
    std::size_t admitted_jobs = 0;     ///< Actual accepted roots/children, including failed scopes.
    bool failed = false;               ///< Later phases are forbidden until a newer frame begins.
    bool closed = true;                ///< End-frame completed successfully, or no frame has begun.
};

/// @brief One application-owned dispatcher; execute is synchronous and never waits from a worker.
class frame_executor
{
  public:
    explicit frame_executor(
        jobs::job_dispatcher dispatcher, frame_limits limits = {},
        std::shared_ptr<std::pmr::memory_resource> memory = {},
        diagnostics::services destinations = {}); ///< Reserve bounded phase names.
    ~frame_executor(); ///< Destroy on the owner after returning from all callbacks.
    frame_executor(const frame_executor&) = delete;            ///< A frame has one coordinator.
    frame_executor& operator=(const frame_executor&) = delete; ///< Never alias mutable frame state.
    void begin_frame(std::uint64_t frame,
                     jobs::job_options options = {}); ///< Explicit per-frame priority/token.
    void end_frame(); ///< Require all attempted phases succeeded; no job can escape this boundary.
    frame_statistics statistics() const; ///< Owner-only, non-reentrant observations.

    template <typename Function>
        requires std::invocable<Function&> && std::destructible<Function>
    void dispatch(std::string_view name, Function function)
    {
        detail::require_phase_function(function);
        serial(name, static_cast<void*>(&function),
               [](void* payload)
               {
                   std::invoke(*static_cast<Function*>(payload));
               });
    } ///< Run lightweight command/event production or publication on the calling owner.

    template <typename Output, typename Input, typename Function>
        requires std::default_initializable<Output> && std::destructible<Function> &&
                 (std::is_empty_v<Function> ||
                  (std::is_pointer_v<Function> &&
                   std::is_function_v<std::remove_pointer_t<Function>>)) &&
                 std::invocable<Function&, const phase_range<Input, Output>&> &&
                 std::same_as<std::invoke_result_t<Function&, const phase_range<Input, Output>&>,
                              void>
    phase_results<Output> run_phase(std::string_view name, std::span<const Input> inputs,
                                    Function function)
    {
        detail::require_phase_function(function);
        detail::typed_phase_task<Input, Output, Function> work{inputs, std::move(function)};
        execute(name, inputs.size(), sizeof(Output), work);
        return phase_results<Output>{std::move(work.output)};
    } ///< Allocate privately, join accepted ranges, then publish immutable ordered outputs.
  private:
    void serial(std::string_view name, void* payload,
                void (*function)(void*)); ///< Checked serial scope.
    void execute(std::string_view name, std::size_t count, std::size_t output_size,
                 detail::phase_task& task); ///< Private erased execution with complete cleanup.
    class impl; ///< Owner/phase guards, bounded names and injected non-owning dispatcher.
    std::unique_ptr<impl> impl_; ///< No hidden scheduler or asynchronous lifetime.
};
} // namespace atlas::ui
