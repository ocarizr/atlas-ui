#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace atlas::diagnostics
{
struct profiling_options
{
    bool enabled{true};
    std::size_t max_events{65536};
    std::size_t max_string_bytes{std::size_t{8} * 1024 * 1024};
    std::uint32_t process_id{1};
};
struct profile_statistics
{
    std::size_t recorded{0};
    std::size_t dropped{0};
    std::size_t errors{0};
    std::size_t string_bytes{0};
};
struct event_context
{
    std::uint64_t id{0};
    std::uint64_t parent_id{0};
    std::string_view result;
    std::uint32_t priority{0};
    std::chrono::nanoseconds dependency_wait{}, queue_wait{}, join{}, total{};
};

void validate(const profiling_options& options);

class profiler
{
  public:
    using clock = std::chrono::steady_clock;
    explicit profiler(profiling_options options = {});
    ~profiler();
    profiler(const profiler&) = delete;
    profiler& operator=(const profiler&) = delete;
    bool duration(std::string_view name, std::string_view category, clock::time_point start,
                  clock::time_point end, event_context context = {},
                  std::thread::id thread = std::this_thread::get_id()) noexcept;
    bool instant(std::string_view name, std::string_view category,
                 event_context context = {}) noexcept;
    bool counter(std::string_view name, std::string_view category, double value) noexcept;
    [[nodiscard]] profile_statistics statistics() const;
    void clear();
    // Takes an owning snapshot before writing. Storage and stream lifetime belong to the caller.
    void write_chrome_trace(std::ostream& output) const;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

class profile_scope
{
  public:
    // Labels are owned; the profiler must survive this scope.
    profile_scope(profiler& capture, std::string_view name, std::string_view category) noexcept;
    ~profile_scope();
    profile_scope(const profile_scope&) = delete;
    profile_scope& operator=(const profile_scope&) = delete;

  private:
    profiler& capture_;
    std::string name_, category_;
    profiler::clock::time_point start_;
    bool ready_{false};
};
} // namespace atlas::diagnostics
