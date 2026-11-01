/**
 * @file
 * @brief Declared-memory access scopes and bounded misuse diagnostics.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace atlas::jobs
{
namespace detail
{
/// @brief Synchronized live declared-access registrations with bounded conflict/escape findings.
class access_registry;
} // namespace detail

/// @brief Declared read/write intent used by the optional live-scope checker.
enum class access_mode : std::uint8_t
{
    /// @brief Allow shared declared read scopes on the region.
    read,
    /// @brief Require exclusive declared write access against overlapping live scopes.
    write
};

/// @brief Borrowed contiguous byte-region declaration; it neither pins memory nor performs
/// accesses.
struct memory_access
{
    /// @brief Borrowed start address; acquiring a guard does not own or pin the pointed-to memory.
    const void* data;
    /// @brief Extent in bytes of this declared contiguous memory region.
    std::size_t bytes;
    /// @brief Declared read/write access intent for a byte range.
    access_mode mode;
};

/// @brief Describe a borrowed read region; the declaration does not perform or own the access.
///
/// @param values Borrowed range; keep its elements alive until the returned handle joins all
/// chunks.
template <typename T, std::size_t Extent> memory_access read_access(std::span<T, Extent> values)
{
    return {values.data(), values.size_bytes(), access_mode::read};
}

/// @brief Describe a borrowed mutable region; const spans cannot request write access.
///
/// @param values Borrowed range; keep its elements alive until the returned handle joins all
/// chunks.
template <typename T, std::size_t Extent>
    requires(!std::is_const_v<T>)
memory_access write_access(std::span<T, Extent> values)
{
    return {values.data(), values.size_bytes(), access_mode::write};
}

/// @brief Declared-access contract violation or cache-line contention heuristic.
enum class diagnostic_kind : std::uint8_t
{
    /// @brief Overlapping declared write/read or write/write scopes from different jobs.
    conflicting_access,
    /// @brief Disjoint declared regions share an assumed cache line; this is advisory.
    possible_false_sharing,
    /// @brief A guard survived its callback and cannot safely keep a live registration.
    escaped_access
};
/// @brief Error versus advisory warning classification for bounded access diagnostics.
enum class diagnostic_severity : std::uint8_t
{
    /// @brief Failure-level logging or a rejected declared-access contract violation.
    error,
    /// @brief Advisory severity that does not itself fail a task.
    warning
};

/// @brief One bounded declared-access finding with both participating task labels.
struct job_diagnostic
{
    /// @brief Conflict, escaped guard or advisory false-sharing classification.
    diagnostic_kind kind;
    /// @brief Whether the finding is a contract error or an advisory warning.
    diagnostic_severity severity;
    /// @brief Owned diagnostic label of the task reporting this finding.
    std::string job_name;
    /// @brief Diagnostic label of the other task participating in the access finding.
    std::string other_job_name;
};

/// @brief Explicit cache-line heuristic and bounded report capacity for declared-access checks.
struct job_sanitizer_options
{
    /// @brief Configured power-of-two cache-line heuristic; this is not hardware discovery.
    std::size_t cache_line_size{64}; /// Configurable heuristic, not hardware discovery.
    /// @brief Maximum retained access findings; excess observations increment dropped counts.
    std::size_t diagnostic_limit{128};
};

/// @brief Owning diagnostic snapshot; absent findings do not prove race freedom.
struct job_sanitizer_report
{
    /// @brief Whether active-scope instrumentation was compiled/enabled for this runtime.
    bool enabled;
    /// @brief Owning collection of retained declared-access findings.
    std::vector<job_diagnostic> diagnostics;
    /// @brief Findings discarded after report capacity; conflict rejection still remains active.
    std::size_t dropped;
};

/// @brief Contract error raised before granting a conflicting or invalid access scope.
class job_sanitizer_error : public std::logic_error
{
  public:
    using std::logic_error::logic_error;
};

/// Declare every access before touching the memory. The guard belongs to this
/// callback's scope; it neither owns the memory nor performs the user's accesses.
class access_guard
{
  public:
    /// @brief Initialize move-only declared-access registration; destroy it inside the callback
    /// before the last access ends.
    access_guard() = default;
    /// @brief Retire this live access registration without throwing.
    ~access_guard();
    /// @brief Initialize move-only declared-access registration; destroy it inside the callback
    /// before the last access ends.
    access_guard(access_guard&& other) noexcept;
    /// @brief Transfer the owned state and leave the moved-from object safely destructible.
    access_guard& operator=(access_guard&& other) noexcept;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    access_guard(const access_guard&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    access_guard& operator=(const access_guard&) = delete;

  private:
    friend class job_context;
    /// @brief Initialize move-only declared-access registration; destroy it inside the callback
    /// before the last access ends.
    access_guard(std::shared_ptr<detail::access_registry> registry, std::size_t token);
    /// @brief Retire owned registration state; scoped cleanup must not throw.
    void release() noexcept;
    /// @brief Shared declared-access registry owning this guard's live token.
    std::shared_ptr<detail::access_registry> registry_;
    /// @brief Exclusive registration identity retired by the guard's cleanup.
    std::size_t token_{0};
};
} // namespace atlas::jobs
