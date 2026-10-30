#pragma once

#include <atlas/jobs/job_system.hpp>
#include <gtest/gtest.h>

#include <future>
#include <mutex>

namespace atlas::jobs::tests
{
class signal_gate
{
  public:
    void open()
    {
        std::call_once(opened_, [this] { promise_.set_value(); });
    }

    void wait() const
    {
        future_.wait();
    }

  private:
    std::promise<void> promise_;
    std::shared_future<void> future_{promise_.get_future().share()};
    std::once_flag opened_;
};

class job_system_fixture : public testing::Test
{
  protected:
    void TearDown() override
    {
        gate.open();
        system.shutdown();
    }

    job_system system{2};
    signal_gate gate;
};
} // namespace atlas::jobs::tests
