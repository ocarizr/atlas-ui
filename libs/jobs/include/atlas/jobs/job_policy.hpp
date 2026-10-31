#pragma once

#include <cstdint>
#include <exception>
#include <stop_token>

namespace atlas::jobs
{
enum class job_priority : std::uint8_t
{
    high,
    normal,
    background
};

struct job_options
{
    job_priority priority{job_priority::normal};
    std::stop_token cancellation{};
};

class job_cancelled : public std::exception
{
  public:
    [[nodiscard]] const char* what() const noexcept override
    {
        return "Job cancelled";
    }
};
} // namespace atlas::jobs
