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
enum class metric_kind : std::uint8_t
{
    counter,
    gauge
};
using metric_attributes = std::vector<std::pair<std::string, std::string>>;

struct metrics_options
{
    bool enabled{true};
    std::size_t max_series{1024};
    std::size_t max_attributes{8};
    std::size_t max_text_bytes{256};
};
struct metric_point
{
    std::string name;
    metric_attributes attributes;
    metric_kind kind{metric_kind::counter};
    double value{0};
};
struct metric_snapshot
{
    std::chrono::system_clock::time_point started{}, captured{};
    std::vector<metric_point> points;
};
struct metrics_statistics
{
    std::size_t series{0};
    std::uint64_t accepted{0}, rejected{0}, dropped{0}, exported{0}, export_errors{0};
};

// Implement transport/platform mapping here. No output is installed automatically.
class metrics_output
{
  public:
    virtual ~metrics_output() = default;
    virtual void publish(const metric_snapshot& snapshot) = 0;
};

void validate(const metrics_options& options);

class metrics
{
  public:
    explicit metrics(metrics_options options = {});
    ~metrics();
    metrics(const metrics&) = delete;
    metrics& operator=(const metrics&) = delete;
    // Counter increments must be finite and nonnegative; gauges accept finite values.
    bool increment(std::string_view name, double amount = 1,
                   const metric_attributes& attributes = {}) noexcept;
    bool set(std::string_view name, double value,
             const metric_attributes& attributes = {}) noexcept;
    // Owning, cumulative snapshot; may throw on allocation failure. Does not reset values.
    [[nodiscard]] metric_snapshot snapshot() const;
    [[nodiscard]] metrics_statistics statistics() const;
    // Synchronous explicit delivery outside collection locks, on the calling thread.
    bool report(metrics_output& output) noexcept;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::diagnostics
