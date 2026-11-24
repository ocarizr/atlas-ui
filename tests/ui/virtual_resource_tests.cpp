/** @file @brief Allocation rollback, resource retirement and shared dispatcher admission. */
#include "resource_test_support.hpp"
#include "virtual_list_support.hpp"

namespace atlas::ui::tests
{
/// @brief Exact failure point setup and teardown stay local to each injected allocation case.
class virtual_resource_probe
{
  public:
    virtual_resource_probe(jobs::job_dispatcher dispatcher)
        : tree(tree_limits{64}, memory), input(tree, {}, memory), viewport(tree.create()),
          list(
              tree, input, initialize(), std::move(dispatcher),
              [](row_context&)
              {
              },
              {}, memory)
    {
        source = std::make_shared<fixed_item_source>(fixed_item_options{100, 20});
        list.update(source, 80);
    } ///< Construct baseline state before enabling failure injection.

    element_handle initialize()
    {
        tree.set_panel(viewport, scroll_layout{});
        tree.set_viewport({0, 0, 100, 80});
        return viewport;
    } ///< The list borrows an already configured scroll domain.

    std::shared_ptr<failing_resource> memory = std::make_shared<failing_resource>(); ///< PMR probe.
    element_tree tree;                         ///< Fixed bounded capacity.
    input_session input;                       ///< Shared cleanup ownership.
    element_handle viewport;                   ///< Single domain.
    virtual_list list;                         ///< Destroy before its borrowers.
    std::shared_ptr<const item_source> source; ///< Immutable input.
};

/// @brief Every PMR allocation point in a replacement preserves the previous session on failure.
TEST(virtual_resources, allocation_failures_leave_no_partial_publication_or_leaked_bytes)
{
    jobs::job_system workers{2};
    std::size_t allocations = 0;
    {
        virtual_resource_probe probe{workers.dispatcher()};
        probe.list.request_focus(90);
        const auto before = probe.memory->calls;
        probe.list.update(probe.source, 80);
        allocations = probe.memory->calls - before;
    }
    ASSERT_GT(allocations, 0);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        virtual_resource_probe probe{workers.dispatcher()};
        const auto snapshot = probe.tree.snapshot();
        const auto ticket = probe.list.rows().front().ticket;
        const auto bytes = probe.memory->live_bytes;
        probe.list.request_focus(90);
        probe.memory->remaining = failure;
        EXPECT_THROW(probe.list.update(probe.source, 80), std::bad_alloc);
        probe.memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(probe.tree.snapshot().revision(), snapshot.revision());
        EXPECT_TRUE(probe.list.accepts(ticket));
        EXPECT_EQ(probe.memory->live_bytes, bytes);
        probe.list.update(probe.source, 80);
        EXPECT_EQ(probe.list.focused_item(), 90);
    }
}

/// @brief Resource leases survive removal until handlers have received disable/end cleanup.
TEST_F(virtual_list_fixture, resources_and_handlers_retire_before_rebinding_can_receive_events)
{
    std::weak_ptr<int> retained;
    auto old_handler = std::make_shared<recording_handler>();
    old_handler->action = [](const routed_event& event, command_buffer&)
    {
        return event_response{false, event.kind == event_kind::pointer_down};
    };
    building = [&](row_context& context)
    {
        if (context.row().item.key == 1)
        {
            auto resource = std::make_shared<int>(42);
            retained = resource;
            context.retain(resource);
            context.bind(context.root(), old_handler);
        }
    };
    update();
    open();
    const std::array press{input_event{input_kind::pointer_down, {10, 10}}};
    input.process(press);
    EXPECT_EQ(input.captured(), list->rows().front().ticket.root);
    finish();
    const auto ticket = list->rows().front().ticket;
    scroll(1000);
    EXPECT_FALSE(list->accepts(ticket));
    EXPECT_FALSE(retained.expired());
    open();
    EXPECT_TRUE(retained.expired());
    EXPECT_EQ(input.captured(), element_handle{});
    const auto ended = std::count_if(old_handler->events.begin(), old_handler->events.end(),
                                     [](const auto& event)
                                     {
                                         return event.kind == event_kind::end;
                                     });
    EXPECT_EQ(ended, 1);
    EXPECT_EQ(std::count_if(old_handler->events.begin(), old_handler->events.end(),
                            [](const auto& event)
                            {
                                return event.kind == event_kind::capture_lost;
                            }),
              1);
    input.process({});
    finish();
}

/// @brief A stable source/range avoids private tree copies and repeated row factory work.
TEST(virtual_resources, unchanged_publication_has_no_pmr_allocations)
{
    jobs::job_system workers{2};
    virtual_resource_probe probe{workers.dispatcher()};
    const auto calls = probe.memory->calls;
    const auto revision = probe.tree.snapshot().revision();
    const auto after_snapshot = probe.memory->calls;
    probe.list.update(probe.source, 80);
    EXPECT_EQ(probe.memory->calls, after_snapshot);
    EXPECT_EQ(probe.tree.snapshot().revision(), revision);
    EXPECT_EQ(probe.list.statistics().created, 0);
    EXPECT_GT(after_snapshot, calls);
}

/// @brief Metadata can use bounded coarse ranges on the host dispatcher, with identical output.
TEST_F(virtual_list_fixture, opt_in_parallel_metadata_joins_before_publication)
{
    list.reset();
    virtual_list_limits limits;
    limits.min_parallel_items = 1;
    limits.grain = 1;
    limits.max_chunks = 2;
    list = std::make_unique<virtual_list>(
        tree, input, viewport, workers.dispatcher(),
        [](row_context&)
        {
        },
        limits);
    update();
    EXPECT_GT(list->statistics().admitted_jobs, 0);
    EXPECT_LE(list->statistics().admitted_jobs, 3);
    EXPECT_EQ(list->rows().size(), 6);
    EXPECT_EQ(list->rows().back().item.offset, 100);
}
} // namespace atlas::ui::tests
