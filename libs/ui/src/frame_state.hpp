/** @file @brief Fixed phase-name budget and coordinator-only frame state. */
#pragma once
#include <array>
#include <atlas/jobs/execution.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <thread>

namespace atlas::ui
{
/// @brief Shared implementation for serial dispatch and joined range execution.
class frame_executor::impl
{
  public:
    /// @brief Names use fixed storage so phase admission does not allocate.
    struct phase_name
    {
        std::array<char, 64> bytes{}; ///< At most 64 bytes, with no control characters.
        std::size_t length = 0;       ///< Explicit length; no terminator is required.
    };

    /// @brief Restore reentrancy protection after success, allocation failure or kernel failure.
    class guard
    {
      public:
        explicit guard(bool& active) : active_(active)
        {
            active_ = true;
        } ///< Enter checked scope.

        ~guard()
        {
            active_ = false;
        } ///< Diagnostics unwind before this reset.

        guard(const guard&) = delete;            ///< One scope owns the reset.
        guard& operator=(const guard&) = delete; ///< Never duplicate a guard.
      private:
        bool& active_; ///< Borrowed coordinator-only flag.
    };

    /// @brief Retain destinations and reserve phase admission storage.
    impl(jobs::job_dispatcher client, frame_limits policy,
         std::shared_ptr<std::pmr::memory_resource> resource, diagnostics::services services);
    void check() const; ///< Reject workers, foreign coordinators and reentrant calls.
    void
    validate(std::string_view name) const; ///< Validate names/state/capacity without admission.
    phase_identity enter(std::string_view name); ///< Record a validated attempted phase.
    void cancellation_point() const; ///< Throw the original cooperative cancellation type.
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole mutable-state owner.
    jobs::job_dispatcher dispatcher;                          ///< Non-owning injected pool client.
    frame_limits limits;                                      ///< Immutable admission/grain policy.
    std::shared_ptr<std::pmr::memory_resource> memory;        ///< Outlives PMR phase-name storage.
    diagnostics::services destinations; ///< Retained logging/capture endpoints.
    std::pmr::vector<phase_name> names; ///< Bounded distinct names in stable phase order.
    jobs::job_options options; ///< Explicit frame cancellation/priority, shared by both paths.
    frame_statistics stats;    ///< Observations of this frame, including failed attempted work.
    bool busy = false;         ///< Covers output construction, joins and diagnostic callbacks.
};
} // namespace atlas::ui
