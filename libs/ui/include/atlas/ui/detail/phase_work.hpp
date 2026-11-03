/**
 * @file
 * @brief Typed range adaptation preserving exclusive outputs and declared job accesses.
 */
#pragma once
#include <atlas/jobs/job_dispatcher.hpp>
#include <atlas/ui/phase_results.hpp>
#include <concepts>
#include <functional>
#include <stop_token>

namespace atlas::ui
{
/// @brief Borrowed for one invocation only; outputs cover the same exclusive index interval as
/// inputs.
template <typename Input, typename Output> struct phase_range
{
    std::span<const Input> inputs; ///< Stable input slice; no live tree pointers are supplied.
    std::span<Output> outputs;     ///< Equal-length disjoint output slice, unpublished until join.
    std::size_t first;             ///< Original stable offset for deterministic indexing.
    std::stop_token cancellation;  ///< Cooperative frame token; expensive kernels should poll it.

    void throw_if_cancelled() const
    {
        if (cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }
    } ///< Checkpoints do not roll back external side effects from arbitrary consumer code.
};

namespace detail
{
/// @brief Reject null callbacks without depending on scheduler implementation helpers.
template <typename Function> void require_phase_function(const Function& function)
{
    if constexpr (std::is_pointer_v<Function>)
    {
        if (!function)
        {
            throw std::invalid_argument("UI phase callback cannot be null");
        }
    }
}

/// @brief Type erasure occurs once per coarse range, never per element.
class phase_task
{
  public:
    virtual ~phase_task() = default; ///< Borrowed through execution; never escapes the joined call.
    virtual void prepare(std::shared_ptr<std::pmr::memory_resource> memory,
                         phase_identity identity) = 0; ///< Allocate before admitting workers.
    virtual void
    run(std::size_t first, std::size_t count, std::stop_token cancellation,
        jobs::job_context* context) = 0; ///< Null context selects owner-thread execution.
};

/// @brief Explicit payload stores a capture-free kernel and immutable borrowed input.
template <typename Input, typename Output, typename Function>
class typed_phase_task final : public phase_task
{
  public:
    typed_phase_task(std::span<const Input> source, Function kernel)
        : source_(source), kernel_(std::move(kernel))
    {
    } ///< Caller retains inputs until execute returns on success or failure.

    void prepare(std::shared_ptr<std::pmr::memory_resource> memory,
                 phase_identity identity) override
    {
        output =
            std::make_shared<phase_storage<Output>>(std::move(memory), identity, source_.size());
    } ///< No mutable output is visible before every accepted job joins.

    void run(std::size_t first, std::size_t count, std::stop_token cancellation,
             jobs::job_context* context) override
    {
        phase_range<Input, Output> range{source_.subspan(first, count),
                                         std::span<Output>{output->values}.subspan(first, count),
                                         first, cancellation};
        range.throw_if_cancelled();
        if (context)
        {
            const jobs::memory_access read{static_cast<const void*>(range.inputs.data()),
                                           range.inputs.size_bytes(), jobs::access_mode::read};
            const jobs::memory_access write{static_cast<const void*>(range.outputs.data()),
                                            range.outputs.size_bytes(), jobs::access_mode::write};
            auto access = context->access({read, write});
            std::invoke(kernel_, std::as_const(range));
        }
        else
        {
            std::invoke(kernel_, std::as_const(range));
        }

        range.throw_if_cancelled();
    } ///< Each worker holds declarations for the complete borrowed input/output slices.

    std::shared_ptr<phase_storage<Output>>
        output; ///< Published only after execute returns normally.
  private:
    std::span<const Input> source_; ///< Caller-owned stable input retained through join.
    Function kernel_; ///< Empty callable or function pointer; no captured shared mutable state.
};
} // namespace detail
} // namespace atlas::ui
