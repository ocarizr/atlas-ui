/** @file @brief Recording policies and exact-once task publication. */
#include <atlas/renderer/recording.hpp>
#include <stdexcept>

namespace atlas::renderer
{
/// @brief Bound scheduling metadata independently of platform APIs.
void recording_options::validate() const
{
    if (min_draws_per_batch == 0 || max_batches == 0 || max_batches > 64)
    {
        throw std::invalid_argument("recording grain and batch count must be positive and bounded");
    }
}

/// @brief Publish success or the original failure with release ordering.
void recording_task::run()
{
    unsigned pending = 0;
    if (!state_.compare_exchange_strong(pending, 1))
    {
        throw std::logic_error("recording task must execute exactly once");
    }

    try
    {
        record();
        state_.store(2, std::memory_order_release);
    }
    catch (...)
    {
        failure_ = std::current_exception();
        state_.store(3, std::memory_order_release);
        throw;
    }
}

/// @brief Acquire the terminal publication before observing recording results.
void recording_task::verify() const
{
    const auto state = state_.load(std::memory_order_acquire);
    if (state == 3)
    {
        std::rethrow_exception(failure_);
    }

    if (state != 2)
    {
        throw std::logic_error("recording executor did not join every task");
    }
}
} // namespace atlas::renderer
