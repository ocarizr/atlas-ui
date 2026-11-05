/** @file @brief Isolated owner-thread routing fixtures and retained callback recordings. */
#pragma once
#include "paint_test_support.hpp"
#include <atlas/jobs/execution.hpp>
#include <atlas/ui/input_session.hpp>
#include <functional>

namespace atlas::ui::tests
{
/// @brief One test's retained event recording and optional serial behavior.
class recording_handler final : public input_handler
{
  public:
    std::vector<routed_event> events; ///< Recorded values never borrow session payloads.
    std::function<event_response(const routed_event&, command_buffer&)> action; ///< Serial hook.

    event_response on_event(const routed_event& event, command_buffer& commands) override
    {
        events.push_back(event);
        return action ? action(event, commands) : event_response{};
    } ///< Retain observation before optional behavior.
};

/// @brief Isolated tree/session with deterministic publication boundaries.
class input_fixture : public paint_fixture
{
  protected:
    void begin(std::uint64_t frame = 1)
    {
        tree.set_viewport({0, 0, 100, 80});
        const auto source = tree.snapshot();
        input.begin_frame(frame, source, layout.calculate(source));
    } ///< Capture geometry and interaction from one committed boundary.

    void finish()
    {
        input.tick();
        input.late_tick();
        input.end_frame();
        tree.apply(tree.commands().seal());
    } ///< Publication occurs after serial command production.

    std::size_t count(event_kind kind) const
    {
        return static_cast<std::size_t>(std::count_if(handler->events.begin(),
                                                      handler->events.end(),
                                                      [kind](const auto& event)
                                                      {
                                                          return event.kind == kind;
                                                      }));
    } ///< Count semantic notifications independent of route phases.

    input_session input{tree}; ///< Destroy before the borrowed tree.
    std::shared_ptr<recording_handler> handler =
        std::make_shared<recording_handler>(); ///< Per fact.
};
} // namespace atlas::ui::tests
