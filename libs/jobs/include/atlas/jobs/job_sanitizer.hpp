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
class access_registry;
}

enum class access_mode : std::uint8_t
{
    read,
    write
};

struct memory_access
{
    const void* data;
    std::size_t bytes;
    access_mode mode;
};

template <typename T, std::size_t Extent> memory_access read_access(std::span<T, Extent> values)
{
    return {values.data(), values.size_bytes(), access_mode::read};
}

template <typename T, std::size_t Extent>
    requires(!std::is_const_v<T>)
memory_access write_access(std::span<T, Extent> values)
{
    return {values.data(), values.size_bytes(), access_mode::write};
}

enum class diagnostic_kind : std::uint8_t
{
    conflicting_access,
    possible_false_sharing,
    escaped_access
};
enum class diagnostic_severity : std::uint8_t
{
    error,
    warning
};

struct job_diagnostic
{
    diagnostic_kind kind;
    diagnostic_severity severity;
    std::string job_name;
    std::string other_job_name;
};

struct job_sanitizer_options
{
    std::size_t cache_line_size{64}; // Configurable heuristic, not hardware discovery.
    std::size_t diagnostic_limit{128};
};

struct job_sanitizer_report
{
    bool enabled;
    std::vector<job_diagnostic> diagnostics;
    std::size_t dropped;
};

class job_sanitizer_error : public std::logic_error
{
  public:
    using std::logic_error::logic_error;
};

// Declare every access before touching the memory. The guard belongs to this
// callback's scope; it neither owns the memory nor performs the user's accesses.
class access_guard
{
  public:
    access_guard() = default;
    ~access_guard();
    access_guard(access_guard&& other) noexcept;
    access_guard& operator=(access_guard&& other) noexcept;
    access_guard(const access_guard&) = delete;
    access_guard& operator=(const access_guard&) = delete;

  private:
    friend class job_context;
    access_guard(std::shared_ptr<detail::access_registry> registry, std::size_t token);
    void release() noexcept;
    std::shared_ptr<detail::access_registry> registry_;
    std::size_t token_{0};
};
} // namespace atlas::jobs
