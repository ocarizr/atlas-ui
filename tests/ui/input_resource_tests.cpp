/** @file @brief Input metadata admission, bounds and allocator-failure publication guarantees. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
class input_fault_resource final : public std::pmr::memory_resource
{
  public:
    bool reject = false; ///< Armed only after constructor/registration reservation.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (reject)
        {
            throw std::bad_alloc{};
        }

        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } ///< Deterministic owner-thread fault injection.

    void do_deallocate(void* address, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(address, bytes, alignment);
    } ///< Cleanup is never rejected.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< Identity resource semantics.
}; ///< Per-fact allocation state; no shared global failure switch.

/// @brief Preparation allocation failure admits neither callbacks nor a new command-frame ID.
TEST_F(input_fixture, AllocationFailureBeforeAdmissionCanRetryTheSameFrame)
{
    const auto target = tree.create();
    const auto memory = std::make_shared<input_fault_resource>();
    input_session bounded{tree, {}, memory};
    bounded.bind(target, handler);
    tree.set_viewport({0, 0, 100, 80});
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    memory->reject = true;
    EXPECT_THROW(bounded.begin_frame(1, source, geometry), std::bad_alloc);
    EXPECT_TRUE(handler->events.empty());
    EXPECT_EQ(bounded.statistics().frame, 0);
    memory->reject = false;
    bounded.begin_frame(1, source, geometry);
    bounded.process({});
    bounded.tick();
    bounded.late_tick();
    bounded.end_frame();
    tree.apply(tree.commands().seal());
    EXPECT_EQ(count(event_kind::start), 1);
}

/// @brief Invalid source/limits/registration do not consume an existing frame or handler slot.
TEST_F(input_fixture, ForeignGeometryAndBindingLimitsFailBeforeDispatch)
{
    EXPECT_THROW((input_session{tree, {.max_bindings = 0}}), std::invalid_argument);
    input_session bounded{tree, {.max_bindings = 1, .max_events = 1}};
    const auto first = tree.create();
    const auto second = tree.create();
    bounded.bind(first, handler);
    EXPECT_THROW(bounded.bind(second, handler), std::overflow_error);
    EXPECT_THROW(bounded.bind(first, handler), std::invalid_argument);
    element_tree foreign;
    const auto wrong = foreign.snapshot();
    EXPECT_THROW(bounded.begin_frame(1, wrong, layout.calculate(wrong)), std::invalid_argument);
    const auto source = tree.snapshot();
    bounded.begin_frame(1, source, layout.calculate(source));
    const std::array too_many{input_event{}, input_event{}};
    EXPECT_THROW(bounded.process(too_many), std::overflow_error);
    EXPECT_EQ(count(event_kind::process_input), 0);
    bounded.process({});
    bounded.tick();
    bounded.late_tick();
    bounded.end_frame();
    tree.apply(tree.commands().seal());
}

/// @brief An impossible registration can be retired after preflight without consuming a frame.
TEST_F(input_fixture, RetiringAnOverDeepUnstartedTargetAllowsBoundaryRecovery)
{
    const auto parent = tree.create();
    const auto child = tree.create(parent);
    input_session shallow{tree, {.max_route_depth = 1}};
    shallow.bind(child, handler);
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    EXPECT_THROW(shallow.begin_frame(1, source, geometry), std::overflow_error);
    EXPECT_TRUE(handler->events.empty());
    shallow.unbind(child);
    EXPECT_NO_THROW(shallow.begin_frame(1, source, geometry));
    EXPECT_EQ(shallow.statistics().frame, 1);
    shallow.process({});
    shallow.tick();
    shallow.late_tick();
    shallow.end_frame();
    tree.apply(tree.commands().seal());
}

/// @brief Callback work is bounded even when lifecycle dispatch exhausts admission immediately.
TEST_F(input_fixture, CallbackBudgetLatchesFailureWithoutExtraCallbackAttempts)
{
    input_session bounded{tree, {.max_callbacks = 1}};
    bounded.bind(tree.create(), handler);
    const auto source = tree.snapshot();
    EXPECT_THROW(bounded.begin_frame(1, source, layout.calculate(source)), std::overflow_error);
    EXPECT_EQ(bounded.statistics().callbacks, 1);
    EXPECT_EQ(handler->events.size(), 1);
    EXPECT_TRUE(bounded.statistics().failed);
    EXPECT_THROW(bounded.process({}), std::logic_error);
}
} // namespace atlas::ui::tests
