/** @file @brief Logical focus, nested clipping and stable source edits across recycling. */
#include "virtual_list_support.hpp"
#include <atlas/input/key.hpp>

namespace atlas::ui::tests
{
/// @brief Unrealized focus targets are realized/revealed before input can observe their handles.
TEST_F(virtual_list_fixture, first_last_reveal_and_logical_keyboard_navigation)
{
    input.configure_navigation();
    update();
    list->request_focus(100);
    list->update(data, 80);
    EXPECT_FLOAT_EQ(position(), 1920);
    open();
    EXPECT_EQ(input.focused(), list->rows().back().ticket.root);
    const std::array events{
        input_event{input_kind::key_down, {}, static_cast<unsigned>(input::key::home)}};
    input.process(events);
    finish();
    list->update(data, 80);
    EXPECT_EQ(list->focused_item(), 1);
    EXPECT_FLOAT_EQ(position(), 0);
    open();
    EXPECT_EQ(input.focused(), list->rows().front().ticket.root);
    input.process({});
    finish();
}

/// @brief Manual wheel-style movement retains logical focus without snapping back to recycled rows.
TEST_F(virtual_list_fixture, manual_scroll_keeps_logical_focus_and_uses_viewport_proxy)
{
    update();
    list->request_focus(1);
    list->update(data, 80);
    open();
    input.process({});
    finish();
    scroll(1000);
    open();
    EXPECT_EQ(list->focused_item(), 1);
    EXPECT_EQ(input.focused(), viewport);
    input.process({});
    finish();
    EXPECT_FLOAT_EQ(position(), 1000);
}

/// @brief An offscreen overscan row retains its binding but cannot keep physical focus active.
TEST_F(virtual_list_fixture, overscan_focus_uses_proxy_without_revealing_the_old_row)
{
    update();
    list->request_focus(4);
    list->update(data, 80);
    open();
    input.process({});
    finish();
    scroll(80);
    open();
    EXPECT_EQ(input.focused(), viewport);
    EXPECT_EQ(list->focused_item(), 4);
    input.process({});
    finish();
    EXPECT_FLOAT_EQ(position(), 80);
}

/// @brief Source edits retain the leading logical anchor and within-row displacement.
TEST_F(virtual_list_fixture, insertion_reorder_and_async_sizes_preserve_anchor)
{
    const std::array<item_description, 5> original{{{1, 20}, {2, 20}, {3, 20}, {4, 20}, {5, 80}}};
    data = std::make_shared<variable_item_source>(original);
    list->update(data, 80);
    scroll(45);
    const std::array<item_description, 6> changed{
        {{9, 30}, {2, 30}, {1, 20}, {3, 20}, {4, 20}, {5, 80}}};
    data = std::make_shared<variable_item_source>(changed);
    list->update(data, 80);
    EXPECT_FLOAT_EQ(position(), 85);
    const auto row = std::find_if(list->rows().begin(), list->rows().end(),
                                  [](const auto& value)
                                  {
                                      return value.item.key == 3;
                                  });
    ASSERT_NE(row, list->rows().end());
    EXPECT_FLOAT_EQ(row->item.offset, 80);
}

/// @brief Removing focused data chooses the next old logical index, then clamps at the end.
TEST_F(virtual_list_fixture, focused_item_removal_rebases_deterministically)
{
    update(5);
    list->request_focus(3);
    list->update(data, 80);
    const std::array<item_description, 4> changed{{{1, 20}, {2, 20}, {4, 20}, {5, 20}}};
    data = std::make_shared<variable_item_source>(changed);
    list->update(data, 80);
    EXPECT_EQ(list->focused_item(), 4);
}

/// @brief Source removal rebases a queued target that was valid when navigation requested it.
TEST_F(virtual_list_fixture, removed_pending_target_does_not_prevent_source_publication)
{
    update(5);
    list->request_focus(3);
    const std::array<item_description, 4> changed{{{1, 20}, {2, 20}, {4, 20}, {5, 20}}};
    data = std::make_shared<variable_item_source>(changed);
    EXPECT_NO_THROW(list->update(data, 80));
    EXPECT_EQ(list->focused_item(), 4);
}

/// @brief Restoring a retained focus cannot apply #121 anchor compensation a second time.
TEST_F(virtual_list_fixture, logical_anchor_is_not_applied_twice_by_input)
{
    const std::array<item_description, 7> original{
        {{1, 20}, {2, 20}, {3, 20}, {4, 20}, {5, 20}, {6, 20}, {7, 20}}};
    data = std::make_shared<variable_item_source>(original);
    list->request_focus(3);
    list->update(data, 80);
    scroll(40);
    open();
    input.process({});
    finish();
    const std::array<item_description, 8> inserted{
        {{9, 20}, {1, 20}, {2, 20}, {3, 20}, {4, 20}, {5, 20}, {6, 20}, {7, 20}}};
    data = std::make_shared<variable_item_source>(inserted);
    list->update(data, 80);
    EXPECT_FLOAT_EQ(position(), 60);
    open();
    input.process({});
    finish();
    EXPECT_FLOAT_EQ(position(), 60);
}

/// @brief A logical list never steals physical focus from another UI domain.
TEST_F(virtual_list_fixture, moving_focus_outside_the_list_releases_logical_focus)
{
    update();
    list->request_focus(1);
    list->update(data, 80);
    const auto outside = rectangle();
    input.bind(outside, handler, {true, true, true});
    open();
    input.focus(outside);
    input.process({});
    finish();
    list->update(data, 80);
    open();
    EXPECT_EQ(input.focused(), outside);
    EXPECT_EQ(list->focused_item(), 0);
    input.process({});
    finish();
}

/// @brief Focus inside a retained row remains on its child control across unchanged updates.
TEST_F(virtual_list_fixture, retained_child_focus_is_not_replaced_with_row_root)
{
    element_handle child;
    building = [&](row_context& context)
    {
        if (context.row().item.key == 1)
        {
            child = context.tree().create(context.root());
            context.bind(child, handler, {true, true, true});
            child.session = context.row().ticket.root.session;
        }
    };
    update();
    open();
    input.focus(child);
    input.process({});
    finish();
    list->update(data, 80);
    open();
    EXPECT_EQ(input.focused(), child);
    input.process({});
    finish();
}
} // namespace atlas::ui::tests
