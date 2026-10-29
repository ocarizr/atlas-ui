#include <atlas/streaming/AssetStreamer.hpp>

#include "AssetReader.hpp"

#include <condition_variable>
#include <deque>
#include <ios>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace atlas::streaming
{
namespace
{
struct Operation
{
    ReadRequest request;
    ReadCompletion completion;
    std::promise<void> finished;
};

struct StreamerState : std::enable_shared_from_this<StreamerState>
{
    StreamerState(jobs::JobSystem& scheduler, AssetCatalog assets, StreamerLimits budgets)
        : jobs{scheduler}, catalog{std::move(assets)}, limits{budgets}
    {
        if (limits.max_read_bytes == 0 || limits.max_pending_requests == 0 ||
            limits.max_read_bytes >
                static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        {
            throw std::invalid_argument{"Streamer limits must be positive and representable"};
        }
    }

    void retire()
    {
        const std::lock_guard lock{mutex};
        --pending;
        idle.notify_all();
    }

    void schedule(const std::shared_ptr<Operation>& operation,
                  std::optional<ReadResult> prepared = std::nullopt)
    {
        try
        {
            jobs.submit(
                [self = shared_from_this(), operation, prepared = std::move(prepared)]() mutable
                {
                    try
                    {
                        auto result = prepared
                                          ? std::move(*prepared)
                                          : detail::read_asset(self->catalog, operation->request);
                        operation->completion(std::move(result));
                        operation->finished.set_value();
                    }
                    catch (...)
                    {
                        operation->finished.set_exception(std::current_exception());
                    }
                    self->retire();
                });
        }
        catch (...)
        {
            // A stopped scheduler cannot dispatch a callback on the correct thread.
            operation->finished.set_exception(std::current_exception());
            retire();
        }
    }

    void run_io()
    {
        while (true)
        {
            std::shared_ptr<Operation> operation;
            {
                std::unique_lock lock{mutex};
                available.wait(lock, [this] { return !queue.empty() || !accepting; });
                if (queue.empty())
                {
                    return;
                }
                operation = std::move(queue.front());
                queue.pop_front();
            }
            schedule(operation, detail::read_asset(catalog, operation->request));
        }
    }

    jobs::JobSystem& jobs;
    const AssetCatalog catalog;
    const StreamerLimits limits;
    std::mutex mutex;
    std::condition_variable available;
    std::condition_variable idle;
    std::deque<std::shared_ptr<Operation>> queue;
    std::size_t pending{0};
    bool accepting{true};
};
} // namespace

struct AssetStreamer::Impl
{
    Impl(jobs::JobSystem& jobs, AssetCatalog catalog, StreamerLimits limits)
        : state{std::make_shared<StreamerState>(jobs, std::move(catalog), limits)},
          io{[shared = state] { shared->run_io(); }}
    {
    }

    std::shared_ptr<StreamerState> state;
    const std::thread::id coordinator{std::this_thread::get_id()};
    std::jthread io;
};

AssetStreamer::AssetStreamer(jobs::JobSystem& jobs, AssetCatalog catalog, StreamerLimits limits)
    : impl_{std::make_unique<Impl>(jobs, std::move(catalog), limits)}
{
}

AssetStreamer::~AssetStreamer()
{
    try
    {
        shutdown();
    }
    catch (...)
    {
        std::terminate();
    }
}

std::future<void> AssetStreamer::read(ReadRequest request, ReadCompletion completion)
{
    if (!completion)
    {
        throw std::invalid_argument{"An asset read needs a completion callback"};
    }
    if (request.size > impl_->state->limits.max_read_bytes)
    {
        throw std::length_error{"Asset read exceeds the configured byte limit"};
    }
    auto operation = std::make_shared<Operation>();
    operation->request = std::move(request);
    operation->completion = std::move(completion);
    auto finished = operation->finished.get_future();
    auto& state = *impl_->state;
    bool file = false;
    {
        const std::lock_guard lock{state.mutex};
        if (!state.accepting)
        {
            throw std::logic_error{"The asset streamer is shutting down"};
        }
        if (state.pending == state.limits.max_pending_requests)
        {
            throw std::overflow_error{"The asset streamer request budget is exhausted"};
        }
        const auto source = state.catalog.find(operation->request.asset_id);
        file =
            source != state.catalog.end() && std::holds_alternative<LooseFileAsset>(source->second);
        if (file)
        {
            state.queue.push_back(operation);
        }
        ++state.pending;
    }
    if (file)
    {
        state.available.notify_one();
    }
    else
    {
        state.schedule(operation);
    }
    return finished;
}

void AssetStreamer::shutdown()
{
    if (std::this_thread::get_id() != impl_->coordinator)
    {
        throw std::logic_error{"Only the creating coordinator may shut down the streamer"};
    }
    auto& state = *impl_->state;
    {
        const std::lock_guard lock{state.mutex};
        state.accepting = false;
    }
    state.available.notify_all();
    if (impl_->io.joinable())
    {
        impl_->io.join();
    }
    std::unique_lock lock{state.mutex};
    state.idle.wait(lock, [&state] { return state.pending == 0; });
}
} // namespace atlas::streaming
