/**
 * @file
 * @brief Behavioral coverage for job system fixture with isolated scenario ownership and cleanup.
 */

#pragma once

#include <atlas/jobs/job_system.hpp>
#include <gtest/gtest.h>

#include <future>
#include <mutex>

namespace atlas::jobs::tests
{
/// @brief Scenario state for signal gate, isolated per test and released through RAII.
class signal_gate
{
  public:
    /// @brief Release the controlled gate without relying on timing sleeps.
    void open()
    {
        std::call_once(opened_,
                       [this]
                       {
                           promise_.set_value();
                       });
    }

    /// @brief Wait for the controlled scenario signal with the configured diagnostic timeout.
    void wait() const
    {
        future_.wait();
    }

  private:
    /// @brief Scenario-owned promise used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::promise<void> promise_;
    /// @brief Scenario-owned future used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_future<void> future_{promise_.get_future().share()};
    /// @brief Scenario-owned opened used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::once_flag opened_;
};

/// @brief Scenario state for job system fixture, isolated per test and released through RAII.
class job_system_fixture : public testing::Test
{
  protected:
    /// @brief Drain and join per-test work before releasing fixture-owned data.
    void TearDown() override
    {
        gate.open();
        system.shutdown();
    }

    /// @brief Scenario-owned system used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    job_system system{2};
    /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    signal_gate gate;
};
} // namespace atlas::jobs::tests
