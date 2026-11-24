/** @file @brief Failure atomicity and bounded staging cleanup. */
#include "virtual_list_support.hpp"

namespace atlas::ui::tests
{
/// @brief A binding failure never replaces the published tree, source, handlers or tickets.
TEST_F(virtual_list_fixture, failing_bind_keeps_previous_publication_usable)
{
    update();
    const auto before = tree.snapshot();
    const auto ticket = list->rows().front().ticket;
    std::size_t released = 0;
    building = [&](row_context& context)
    {
        context.retain(std::shared_ptr<void>{new int{1}, [&](void* pointer)
                                             {
                                                 delete static_cast<int*>(pointer);
                                                 ++released;
                                             }});
        throw std::runtime_error("binding rejected");
    };
    list->request_focus(90);
    EXPECT_THROW(list->update(data, 80), std::runtime_error);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
    EXPECT_TRUE(list->accepts(ticket));
    EXPECT_EQ(released, 1);
    building = {};
    list->update(data, 80);
    EXPECT_EQ(list->focused_item(), 90);
}

/// @brief Oversized viewports reject admission without allocating one node per logical item.
TEST_F(virtual_list_fixture, visible_admission_failure_does_not_mutate_the_tree)
{
    update();
    const auto before = tree.snapshot();
    const auto large = std::make_shared<fixed_item_source>(fixed_item_options{10000, 20});
    EXPECT_THROW(list->update(large, 10000), std::overflow_error);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
}

/// @brief Cancellation is observed before any row factory can produce side effects.
TEST_F(virtual_list_fixture, cancelled_metadata_never_publishes_rows)
{
    update();
    const auto before = tree.snapshot();
    std::stop_source cancellation;
    cancellation.request_stop();
    jobs::job_options options;
    options.cancellation = cancellation.get_token();
    EXPECT_THROW(list->update(data, 80, options), jobs::job_cancelled);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
    EXPECT_EQ(list->rows().size(), 6);
}

/// @brief Failed staging tickets cannot become valid when the same physical slot is retried.
TEST_F(virtual_list_fixture, failed_ticket_never_aliases_a_later_successful_binding)
{
    row_ticket escaped;
    building = [&](row_context& context)
    {
        escaped = context.row().ticket;
        throw std::runtime_error("discard this binding");
    };
    data = std::make_shared<fixed_item_source>(fixed_item_options{100, 20});
    EXPECT_THROW(list->update(data, 80), std::runtime_error);
    EXPECT_EQ(tree.size(), 1);
    building = {};
    list->update(data, 80);
    EXPECT_EQ(escaped.root, list->rows().front().ticket.root);
    EXPECT_FALSE(list->accepts(escaped));
}

/// @brief An idle frame boundary and per-row subtree bounds are required even for tiny lists.
TEST_F(virtual_list_fixture, active_frame_and_oversized_factory_cannot_publish)
{
    update();
    open();
    EXPECT_THROW(list->update(data, 80), std::logic_error);
    input.process({});
    finish();
    const auto before = tree.snapshot();
    building = [](row_context& context)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            (void)context.tree().create(context.root());
        }
    };
    list->request_focus(90);
    EXPECT_THROW(list->update(data, 80), std::overflow_error);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
}

/// @brief Refused scheduler admission cannot call the factory or replace a live list.
TEST_F(virtual_list_fixture, closed_shared_dispatcher_rejects_parallel_admission)
{
    list.reset();
    virtual_list_limits limits;
    limits.min_parallel_items = 1;
    limits.grain = 1;
    list = std::make_unique<virtual_list>(
        tree, input, viewport, workers.dispatcher(),
        [](row_context&)
        {
        },
        limits);
    workers.shutdown();
    data = std::make_shared<fixed_item_source>(fixed_item_options{100, 20});
    EXPECT_THROW(list->update(data, 80), std::exception);
    EXPECT_EQ(tree.size(), 1);
    EXPECT_TRUE(list->rows().empty());
}

/// @brief Cancellation during serial staging releases new leases without replacing live content.
TEST_F(virtual_list_fixture, cancellation_during_binding_rolls_back_before_publication)
{
    update();
    const auto before = tree.snapshot();
    std::stop_source stop;
    building = [&](row_context&)
    {
        stop.request_stop();
    };
    list->request_focus(90);
    EXPECT_THROW(list->update(data, 80, {jobs::job_priority::normal, stop.get_token()}),
                 jobs::job_cancelled);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
}
} // namespace atlas::ui::tests
