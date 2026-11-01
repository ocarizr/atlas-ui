/**
 * @file
 * @brief Bounded numeric counter/gauge aggregation and explicit transport-neutral snapshots.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atlas::diagnostics
{
/// @brief Cumulative counter versus last-value gauge aggregation policy.
enum class metric_kind : std::uint8_t
{
    /// @brief Cumulative sum of finite nonnegative increments.
    counter,
    /// @brief Most recently accepted finite numeric value for a metric series.
    gauge
};
/// @brief Owned key/value dimensions; order is canonicalized before identifying a metric series.
using metric_attributes = std::vector<std::pair<std::string, std::string>>;

/// @brief Immutable limits for retained series, attributes and UTF-8 label sizes.
struct metrics_options
{
    /// @brief Whether this explicitly created registry accepts observations/reports; it starts no
    /// sampler or transport.
    bool enabled{true};
    /// @brief Maximum distinct metric name/attribute combinations retained by this registry.
    std::size_t max_series{1024};
    /// @brief Maximum dimension pairs per metric series; zero permits unlabelled series only.
    std::size_t max_attributes{8};
    /// @brief Maximum UTF-8 bytes in each metric name, dimension key or dimension value.
    std::size_t max_text_bytes{256};
};

/// @brief One owned numeric series identified by name and canonical key/value dimensions.
struct metric_point
{
    /// @brief Owned case-sensitive instrument name identifying this metric family.
    std::string name;
    /// @brief Owned key/value metric dimensions sorted canonically in snapshots.
    metric_attributes attributes;
    /// @brief Counter or gauge aggregation policy, fixed per name within one registry.
    metric_kind kind{metric_kind::counter};
    /// @brief Cumulative counter total or last accepted finite gauge observation.
    double value{0};
};

/// @brief Owning cumulative series view with wall-clock start/capture timestamps, not a replay
/// record.
struct metric_snapshot
{
    /// @brief Wall-clock registry creation and snapshot-capture times; clock adjustments may move
    /// them backward.
    std::chrono::system_clock::time_point started{}, captured{};
    /// @brief Owned cumulative counter/gauge observations sorted by name and dimensions.
    std::vector<metric_point> points;
};

/// @brief Observational metric admission, rejection, cardinality-loss and export counts.
struct metrics_statistics
{
    /// @brief Current number of retained metric name/dimension combinations.
    std::size_t series{0};
    /// @brief Applied, invalid, capacity-dropped, successful-report and failed-report totals,
    /// respectively.
    std::uint64_t accepted{0}, rejected{0}, dropped{0}, exported{0}, export_errors{0};
};

/// Implement transport/platform mapping here. No output is installed automatically.
class metrics_output
{
  public:
    /// @brief Release metrics output resources and complete its scoped cleanup.
    virtual ~metrics_output() = default;
    /// @brief Accept a call-scoped cumulative snapshot; asynchronous adapters must retain their own
    /// owning copy.
    virtual void publish(const metric_snapshot& snapshot) = 0;
};

/// @brief Reject invalid configuration, handles or descriptors before mutating execution state.
///
/// @param options Explicit validated configuration/policy for this instance.
void validate(const metrics_options& options);

/// @brief Explicit thread-safe counter/gauge registry; no sampling or telemetry transport starts
/// automatically.
///
/// No metrics are sampled or transmitted automatically. Snapshots are cumulative and owning;
/// adapters must not treat each report as a fresh counter increment. Stop callers before
/// destruction.
class metrics
{
  public:
    /// @brief Create an empty bounded numeric registry; observations and reporting are explicit
    /// caller operations.
    ///
    /// @param options Explicit validated configuration/policy for this instance.
    explicit metrics(metrics_options options = {});
    /// @brief Release metrics resources and complete its scoped cleanup.
    ~metrics();
    /// @brief Create an empty bounded numeric registry; observations and reporting are explicit
    /// caller operations.
    metrics(const metrics&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    metrics& operator=(const metrics&) = delete;
    /// Counter increments must be finite and nonnegative; gauges accept finite values.
    bool increment(std::string_view name, double amount = 1,
                   const metric_attributes& attributes = {}) noexcept;
    /// @brief Replace a gauge with a finite value while preserving stable instrument kind per name.
    ///
    /// @param name Case-sensitive diagnostic or metric name; metric names must be nonempty bounded
    /// UTF-8.
    /// @param value Finite numeric observation; gauges accept negative values.
    /// @param attributes Owned copies of bounded dimension pairs; duplicate keys are rejected.
    bool set(std::string_view name, double value,
             const metric_attributes& attributes = {}) noexcept;
    /// Owning, cumulative snapshot; may throw on allocation failure. Does not reset values.
    [[nodiscard]] metric_snapshot snapshot() const;
    /// @brief Return an observational snapshot of this instance's counters without resetting them.
    [[nodiscard]] metrics_statistics statistics() const;
    /// Synchronous explicit delivery outside collection locks, on the calling thread.
    bool report(metrics_output& output) noexcept;

  private:
    /// @brief Private implementation state; callers interact through the enclosing public API.
    struct impl;
    /// @brief Owned private implementation storage; destruction follows the enclosing lifecycle
    /// contract.
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::diagnostics
