/**
 * @file
 * @brief Optional injected library diagnostics with compile-time CPU profiling policy.
 */
#pragma once
#include <atlas/diagnostics/logging.hpp>
#include <atlas/diagnostics/profiling.hpp>
#include <exception>

#ifndef ATLAS_PROFILING_ENABLED
#ifdef NDEBUG
/// @brief Header-only consumers default to no instrumentation in release builds.
#define ATLAS_PROFILING_ENABLED 0
#else
/// @brief Header-only consumers default to instrumentation in debug builds.
#define ATLAS_PROFILING_ENABLED 1
#endif
#endif

namespace atlas::diagnostics
{
/// @brief Whether library profiling probes are compiled for this configuration.
inline constexpr bool instrumentation_enabled = ATLAS_PROFILING_ENABLED != 0;

/// @brief Immutable optional destinations retained by their owning library instance.
struct services
{
    std::shared_ptr<logger> log; ///< Optional structured logger; no implicit backend is created.
    std::shared_ptr<profiler> profile; ///< Optional bounded CPU capture, shared across subsystems.
};

/// @brief Record lifecycle/failure messages without changing library outcomes.
inline void write(const services& destinations, log_level level, std::string_view category,
                  std::string_view message) noexcept
{
    if (destinations.log)
    {
        (void)destinations.log->write(level, category, message);
    }
}

/// @brief Stable borrowed operation identity, separate from its optional capture destination.
struct operation_label
{
    std::string_view name;     ///< Operation name; its storage must outlive the scope.
    std::string_view category; ///< CPU grouping; its storage must outlive the scope.
};

/// @brief Borrow stable labels and destinations for one CPU interval; no file or global capture.
class operation_scope
{
  public:
    /// @brief Destinations and label storage must outlive the measured operation.
    operation_scope(const services& destinations, operation_label label) noexcept
        : destinations_(destinations), name_(label.name), category_(label.category)
    {
        if (destinations_.log)
        {
            exceptions_ = std::uncaught_exceptions();
        }

#if ATLAS_PROFILING_ENABLED
        exceptions_ = std::uncaught_exceptions();
        if (destinations_.profile)
        {
            start_ = profiler::clock::now();
        }
#endif
    }

    /// @brief Record CPU wall duration and exception unwinding only when probes are compiled in.
    ~operation_scope()
    {
        if (destinations_.log && std::uncaught_exceptions() > exceptions_)
        {
            write(destinations_, log_level::error, category_, name_);
        }

#if ATLAS_PROFILING_ENABLED
        if (destinations_.profile)
        {
            event_context context;
            context.result = std::uncaught_exceptions() > exceptions_ ? "failed" : "succeeded";
            (void)destinations_.profile->duration(name_, category_, start_, profiler::clock::now(),
                                                  context);
        }
#endif
    }

    operation_scope(const operation_scope&) = delete; ///< One scope emits at most one interval.
    operation_scope& operator=(const operation_scope&) = delete; ///< Scope ownership is unique.
  private:
    [[maybe_unused]] const services& destinations_;        ///< Borrowed immutable endpoints.
    [[maybe_unused]] std::string_view name_;               ///< Borrowed stable operation label.
    [[maybe_unused]] std::string_view category_;           ///< Borrowed CPU category.
    [[maybe_unused]] profiler::clock::time_point start_{}; ///< Start, untouched when disabled.
    [[maybe_unused]] int exceptions_ = 0; ///< Unwinding baseline for observed failures.
};
} // namespace atlas::diagnostics
