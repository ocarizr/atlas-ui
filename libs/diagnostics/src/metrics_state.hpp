/**
 * @file
 * @brief Canonical metric identities, synchronized updates and export accounting.
 */

#pragma once

#include <atlas/diagnostics/metrics.hpp>

#include <atomic>
#include <map>
#include <mutex>

namespace atlas::diagnostics
{
/// @brief Private implementation state; callers interact through the enclosing public API.
struct metrics::impl
{
    /// @brief Metric identity composed of the owned instrument name and sorted dimensions.
    using key = std::pair<std::string, metric_attributes>;

    /// @brief Stored metric instrument kind and its current cumulative or last-observed value.
    struct value
    {
        /// @brief Aggregation, event or diagnostic discriminator interpreted by the enclosing
        /// contract.
        metric_kind kind;
        /// @brief Current cumulative counter or most recently accepted gauge value.
        double number;
    };

    /// @brief Retain validated immutable registry limits without starting sampling or transport.
    explicit impl(metrics_options configuration) : options{configuration}
    {
        validate(options);
    }

    /// @brief Validate and canonicalize one numeric observation before applying a bounded registry
    /// update.
    bool record(std::string_view name, double number, const metric_attributes& attributes,
                metric_kind kind) noexcept;
    /// @brief Apply one canonical metric update under the registry lock, enforcing type and series
    /// limits.
    bool update(key identity, double number, metric_kind kind);
    /// @brief Explicit policy/configuration retained for this execution or subsystem.
    const metrics_options options;
    /// @brief Wall-clock registry creation time retained across cumulative snapshots.
    const std::chrono::system_clock::time_point started{std::chrono::system_clock::now()};
    /// @brief Synchronization for the enclosing shared registry/capture/execution state.
    mutable std::mutex mutex;
    /// @brief Owned canonical name/dimension keys and their fixed aggregation kind/current value.
    std::map<key, value> values;
    /// @brief Observations successfully applied to the metric registry.
    std::atomic<std::uint64_t> accepted{0}, rejected{0}, dropped{0}, exported{0}, export_errors{0};
};
} // namespace atlas::diagnostics
