/**
 * @file
 * @brief Simultaneously live declared-region validation and bounded findings.
 */

#pragma once

#include <atlas/jobs/job_sanitizer.hpp>

#include <cstdint>
#include <mutex>

namespace atlas::jobs::detail
{
/// @brief Retained direct-task result and join/dependency state; terminal status publishes
/// immutable outcomes.
struct task_state;

/// @brief Validated half-open byte-address interval and declared access intent.
struct access_region
{
    /// @brief Inclusive byte address of a validated access region.
    std::uintptr_t begin;
    /// @brief Exclusive byte address; normalization rejects wrapping intervals.
    std::uintptr_t end;
    /// @brief Declared read/write access intent for a byte range.
    access_mode mode;
};

/// @brief One live token with its originating task and normalized declared regions.
struct active_access
{
    /// @brief Unique live declared-access registration identity.
    std::size_t token;
    /// @brief Identity of the originating live task; used to distinguish its own overlapping
    /// declarations.
    const task_state* owner;
    /// @brief Owned task label retained while this access scope is live.
    std::string name;
    /// @brief Validated contiguous declared-access byte ranges for this registration.
    std::vector<access_region> regions;
};

/// @brief Synchronized live declared-access registrations with bounded conflict/escape findings.
class access_registry
{
  public:
    /// @brief Configure bounded live-access diagnostics and validate the cache-line heuristic.
    explicit access_registry(job_sanitizer_options options);
    /// @brief Validate and register simultaneous declared memory scopes, rejecting conflicting
    /// access.
    std::size_t acquire(const task_state& owner, std::span<const memory_access> requests);
    /// @brief Retire owned registration state; scoped cleanup must not throw.
    void release(std::size_t token) noexcept;
    /// @brief Retire callback-scoped access registrations and report guards escaping their scope.
    bool finish(const task_state& owner);
    /// @brief Copy bounded declared-access findings under the registry mutex; this does not invoke
    /// an output or reset findings.
    job_sanitizer_report report() const;

  private:
    /// @brief Reject incompatible overlapping live declarations and record cache-line heuristic
    /// warnings.
    void check(const active_access& incoming);
    /// @brief Retain a bounded access finding and account for report overflow.
    void record(diagnostic_kind kind, const std::string& name, const std::string& other = {});
    /// @brief Immutable policy/configuration retained by this object.
    job_sanitizer_options options_;
    /// @brief Synchronization for this object's shared mutable storage.
    mutable std::mutex mutex_;
    /// @brief Live normalized declared-memory registrations participating in overlap checks.
    std::vector<active_access> active_;
    /// @brief Bounded declared-access findings retained for reporting.
    std::vector<job_diagnostic> diagnostics_;
    /// @brief Next unique live access registration identity.
    std::size_t next_token_{1};
    /// @brief Number of access findings discarded after the report capacity is reached.
    std::size_t dropped_{0};
};
} // namespace atlas::jobs::detail
