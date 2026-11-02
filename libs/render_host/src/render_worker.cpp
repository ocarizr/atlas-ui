/** @file @brief Exclusive render-thread handler execution and original-failure propagation. */
#include "loop_state.hpp"

namespace atlas::render_host
{
/// @brief Factory, frame bodies, shutdown and graphics destruction all remain on this thread.
void render_loop::impl::run(const frame_factory& factory) noexcept
{
    try
    {
        std::unique_ptr<frame_handler> handler;
        {
            const diagnostics::operation_scope scope{destinations,
                                                     {"render_host.initialize", "host.render"}};
            handler = factory();
            if (!handler)
            {
                throw std::runtime_error("render factory returned no handler");
            }
        }
        diagnostics::write(destinations, diagnostics::log_level::info, "render_host",
                           "Render coordinator initialized");
        for (;;)
        {
            frame_snapshot frame;
            {
                std::unique_lock lock{mutex};
                wake.wait(lock,
                          [&]
                          {
                              return stopping || counts.queued != 0;
                          });
                if (counts.queued == 0)
                {
                    break;
                }

                frame = std::move(queue[head]);
                head = (head + 1) % queue.size();
                --counts.queued;
            }
            const diagnostics::operation_scope scope{destinations,
                                                     {"render_host.frame", "host.render"}};
            try
            {
                handler->render(frame);
            }
            catch (...)
            {
                const std::lock_guard lock{mutex};
                ++counts.failed;
                throw;
            }

            const std::lock_guard lock{mutex};
            ++counts.completed;
        }

        {
            const diagnostics::operation_scope scope{destinations,
                                                     {"render_host.shutdown", "host.render"}};
            handler->shutdown();
            handler.reset();
        }
    }
    catch (...)
    {
        std::vector<frame_snapshot> abandoned;
        {
            const std::lock_guard lock{mutex};
            failure = std::current_exception();
            stopping = true;
            counts.discarded += counts.queued;
            counts.queued = 0;
            abandoned.swap(queue);
        }
        diagnostics::write(destinations, diagnostics::log_level::error, "render_host",
                           "Render coordinator failed; accepted work drained or discarded");
    }

    diagnostics::write(destinations, diagnostics::log_level::info, "render_host",
                       "Render coordinator stopped");
    done.store(true, std::memory_order_release);
}
} // namespace atlas::render_host
