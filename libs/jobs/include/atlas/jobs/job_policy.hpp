/**
 * @file
 * @brief Weighted dispatch classes and cooperative cancellation policy.
 */

#pragma once

#include <cstdint>
#include <exception>
#include <stop_token>

namespace atlas::jobs
{
/// @brief Weighted high/normal/background dispatch classes, not preemption or real-time deadlines.
enum class job_priority : std::uint8_t
{
    /// @brief Highest weighted service class; it does not preempt callbacks already running.
    high,
    /// @brief Default weighted service class used by ordinary and legacy work.
    normal,
    /// @brief Lowest weighted service class with progress while continuously ready.
    background
};

/// @brief Scheduling class and cooperative stop token inherited by attached child scopes.
struct job_options
{
    /// @brief Explicit weighted scheduling class inherited by attached children.
    job_priority priority{job_priority::normal};
    /// @brief Cooperative stop token; running bodies must explicitly check it.
    std::stop_token cancellation{};
};

/// @brief Exception used as the original cause of a cooperatively cancelled task.
class job_cancelled : public std::exception
{
  public:
    /// @brief Return the stable textual description of cooperative job cancellation.
    [[nodiscard]] const char* what() const noexcept override
    {
        return "Job cancelled";
    }
};
} // namespace atlas::jobs
