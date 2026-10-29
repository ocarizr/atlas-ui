#pragma once

#include <atlas/jobs/JobSystem.hpp>
#include <gtest/gtest.h>

#include <future>
#include <mutex>

namespace atlas::jobs::tests
{
class Gate
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

class JobSystemFixture : public testing::Test
{
  protected:
    void TearDown() override
    {
        gate.open();
        system.shutdown();
    }

    JobSystem system{2};
    Gate gate;
};
} // namespace atlas::jobs::tests
