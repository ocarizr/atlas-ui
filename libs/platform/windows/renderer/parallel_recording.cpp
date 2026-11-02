/** @file @brief Joined CPU recording and deterministic direct-queue submission. */
#include "native_passes.hpp"

namespace atlas::renderer::detail
{
namespace
{
/// @brief Only the first and final ordered lists stamp the frame query pair.
struct query_section
{
    gpu_frame_timer& timer; ///< Frame-owned, stable through the joined recording scope.
    bool first;             ///< Stamp before the first recorded operation.
    bool last;              ///< Resolve after the final operation.
};
#if defined(_MSC_VER)
// Native task instances intentionally occupy separate cache lines; tail padding is required.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
/// @brief A worker borrows immutable inputs and owns exactly one recording context.
class alignas(64) native_recording_task final : public recording_task
{
  public:
    native_recording_task(native_passes& owner, pass_batch& context,
                          std::span<const resolved_pass> passes, const recording_group& sections,
                          const diagnostics::services& destinations, query_section timing)
        : owner_(owner), context_(context), passes_(passes), sections_(sections),
          destinations_(destinations), timing_(timing)
    {
    } ///< Bind until the executor joins.
  private:
    /// @brief No storage, presentation, queue submission or fence wait enters the worker body.
    void record() override
    {
        const diagnostics::operation_scope scope{destinations_,
                                                 {"renderer.record.batch", "renderer.cpu_record"}};
        if (timing_.first)
        {
            timing_.timer.begin(*context_.commands.Get());
        }

        ID3D12DescriptorHeap* heaps[]{owner_.resources->descriptors.Get()};
        context_.commands->SetDescriptorHeaps(1, heaps);
        for (const auto& section : sections_)
        {
            owner_.record(context_, passes_[section.pass], section);
        }

        if (timing_.last)
        {
            timing_.timer.end(*context_.commands.Get());
        }

        check(context_.commands->Close(), "finish recording batch");
    }

    native_passes& owner_; ///< Immutable backend services during this joined operation.
    pass_batch& context_;  ///< Exclusive list, allocator and attachment descriptors.
    std::span<const resolved_pass> passes_; ///< Coordinator-validated immutable resource bindings.
    const recording_group& sections_;       ///< Stable contiguous assignment.
    const diagnostics::services& destinations_; ///< Inherited thread-safe diagnostics endpoints.
    query_section timing_; ///< Exclusive start/end query indices in ordered GPU submission.
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace

/// @brief Finish every recording task before publishing command lists in original frame order.
std::uint64_t native_passes::submit(std::span<const resolved_pass> passes,
                                    recording_executor* executor, recording_options options,
                                    const diagnostics::services& destinations)
{
    const diagnostics::operation_scope scope{destinations,
                                             {"renderer.record.join", "renderer.cpu"}};
    resources->context->ensure_healthy("submit graphics passes");
    if (!executor)
    {
        options.max_batches = 1;
    }

    std::vector<std::size_t> counts;
    counts.reserve(passes.size());
    std::uint64_t draws = 0;
    for (const auto& pass : passes)
    {
        counts.push_back(pass.draws.size());
        draws += pass.draws.size();
    }

    const auto groups = plan_recording(counts, options);
    auto frame = begin(groups.size());
    frame.timer.prepare(*resources->context, destinations);
    std::vector<std::unique_ptr<native_recording_task>> owners;
    std::vector<recording_task*> tasks;
    std::vector<ID3D12CommandList*> lists;
    owners.reserve(groups.size());
    tasks.reserve(groups.size());
    lists.reserve(groups.size());
    for (std::size_t index = 0; index < groups.size(); ++index)
    {
        owners.push_back(std::make_unique<native_recording_task>(
            *this, frame.batches[index], passes, groups[index], destinations,
            query_section{frame.timer, index == 0, index + 1 == groups.size()}));
        tasks.push_back(owners.back().get());
        lists.push_back(frame.batches[index].commands.Get());
    }

    const bool parallel = tasks.size() > 1;
    if (parallel && executor)
    {
        executor->execute(tasks);
    }
    else
    {
        tasks.front()->run();
    }

    for (const auto* task : tasks)
    {
        task->verify();
    }

    resources->context->ensure_healthy("execute graphics passes");
    resources->context->queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
    try
    {
        frame.fence = resources->context->signal();
    }
    catch (...)
    {
        resources->drain();
        throw;
    }

    const auto fence = frame.fence;
    pending.push_back(std::move(frame));
    statistics = {static_cast<std::uint32_t>(tasks.size()),
                  parallel ? static_cast<std::uint32_t>(tasks.size()) : 0, draws};
    return fence;
}

/// @brief Select the native adapter without creating another queue or scheduler.
std::unique_ptr<pass_backend> make_pass_backend(const std::shared_ptr<resource_backend>& resources,
                                                pass_limits limits)
{
    return std::make_unique<native_passes>(std::static_pointer_cast<native_resources>(resources),
                                           limits);
}
} // namespace atlas::renderer::detail
