#pragma once

#include <atlas/diagnostics/metrics.hpp>

#include <atomic>
#include <map>
#include <mutex>

namespace atlas::diagnostics
{
struct metrics::impl
{
    using key = std::pair<std::string, metric_attributes>;
    struct value
    {
        metric_kind kind;
        double number;
    };
    explicit impl(metrics_options configuration) : options{configuration}
    {
        validate(options);
    }
    bool record(std::string_view name, double number, const metric_attributes& attributes,
                metric_kind kind) noexcept;
    bool update(key identity, double number, metric_kind kind);
    const metrics_options options;
    const std::chrono::system_clock::time_point started{std::chrono::system_clock::now()};
    mutable std::mutex mutex;
    std::map<key, value> values;
    std::atomic<std::uint64_t> accepted{0}, rejected{0}, dropped{0}, exported{0}, export_errors{0};
};
} // namespace atlas::diagnostics
