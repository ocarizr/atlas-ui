/** @file @brief Two independently owned panels in one logical native window. */
#pragma once
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Isolate drag participants, callbacks and document history for each test.
class drag_fixture : public paint_fixture
{
  protected:
    void SetUp() override
    {
        source = tree.create();
        target = other.create();
        input.bind(source, source_handler, {.focusable = true});
        destination.bind(target, target_handler, {.focusable = true});
        input.configure_drag_source(
            source, {drag_payload::text("test.item", "item-42"), drag_operations::copy_move});
        destination.configure_drop_target(target, {{"test.item"}, drag_operations::copy_move});
    } ///< Every test owns its trees, source buffers and retained callback recordings.

    void begin(std::uint64_t frame = 1)
    {
        tree.set_viewport({0, 0, 100, 80});
        other.set_viewport({100, 0, 100, 80});
        auto first = tree.snapshot();
        auto second = other.snapshot();
        input.begin_frame(frame, first, layout.calculate(first));
        destination.begin_frame(frame, second, layout.calculate(second));
        input.process({});
        destination.process({});
        std::array views{&input, &destination};
        drag.begin_frame(frame, views);
    } ///< Empty initial input allows the shared domain to route subsequent packets exactly once.

    void finish()
    {
        drag.end_frame();
        for (auto* owner : {&input, &destination})
        {
            owner->tick();
            owner->late_tick();
            owner->end_frame();
        }

        tree.apply(tree.commands().seal());
        other.apply(other.commands().seal());
    } ///< Release the domain before closing any participating input frame.

    bool packet(input_event event)
    {
        if (drag.process(event))
        {
            return true;
        }

        input.process_more(std::span{&event, 1});
        destination.process_more(std::span{&event, 1});
        return false;
    } ///< Preserve ordinary routing below threshold.

    void pointer_drop()
    {
        EXPECT_FALSE(packet({input_kind::pointer_down, {10, 10}}));
        EXPECT_TRUE(packet({input_kind::pointer_move, {150, 20}}));
        EXPECT_TRUE(packet({input_kind::pointer_up, {150, 20}}));
    } ///< The application has not mutated anything when a ticket becomes pending.

    drop_request pending() const
    {
        const auto value = drag.pending_drop();
        if (!value)
        {
            throw std::logic_error("Expected a pending drag transfer");
        }

        return *value;
    } ///< Fail explicitly before accessing an absent request; shared by positive-path facts.

    std::size_t count(const recording_handler& handler, event_kind kind) const
    {
        return std::count_if(handler.events.begin(), handler.events.end(),
                             [&](const auto& event)
                             {
                                 return event.kind == kind;
                             });
    } ///< Ignore unrelated lifecycle delivery.

    element_tree other;                            ///< Separate panel, not another native window.
    input_session input{tree}, destination{other}; ///< Destroy before their borrowed trees.
    drag_drop_session drag;            ///< Destroy first; weak leases do not prolong input owners.
    element_handle source{}, target{}; ///< Full generation identities.
    std::shared_ptr<recording_handler> source_handler =
        std::make_shared<recording_handler>(); ///< Per-test recorder.
    std::shared_ptr<recording_handler> target_handler =
        std::make_shared<recording_handler>(); ///< Per-test recorder.
};
} // namespace atlas::ui::tests
