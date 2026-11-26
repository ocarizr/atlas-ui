/** @file @brief Focus fixtures build small positioned widgets without shared mutable state. */
#pragma once
#include "input_test_support.hpp"
#include <atlas/input/key.hpp>

namespace atlas::ui::tests
{
/// @brief Every test has one canvas, owner session and retained handler.
class focus_navigation_fixture : public input_fixture
{
  protected:
    focus_navigation_fixture()
    {
        tree.set_panel(root, canvas_layout{});
    } ///< Root uses explicit positions.

    element_handle button(layout_point position, element_handle parent = {})
    {
        if (!parent.generation)
        {
            parent = root;
        }

        const auto target = rectangle(parent);
        layout_style style;
        style.intrinsic = {10, 10};
        tree.set_layout(target, style);
        canvas_slot slot;
        slot.horizontal.offset_min = position.x;
        slot.vertical.offset_min = position.y;
        tree.set_canvas_slot(target, slot);
        input.bind(target, handler, {.focusable = true});
        return target;
    } ///< Geometry determines automatic destinations; identities retain generations.

    element_handle group(layout_point position)
    {
        const auto target = rectangle(root);
        layout_style style;
        style.intrinsic = {50, 20};
        tree.set_layout(target, style);
        tree.set_panel(target, canvas_layout{});
        canvas_slot slot;
        slot.horizontal.offset_min = position.x;
        slot.vertical.offset_min = position.y;
        tree.set_canvas_slot(target, slot);
        return target;
    } ///< A group root need not be an interactive binding itself.

    input_event key(input::key value, input_kind kind = input_kind::key_down) const
    {
        return {kind, {}, static_cast<std::uint32_t>(value)};
    } ///< Portable keyboard replay.

    input_event action(input::action_id id,
                       input::action_phase phase = input::action_phase::performed) const
    {
        input_event result{input_kind::action};
        result.action = input::action_event{id, input::value_type::button, phase, {1, 0}, 1};
        return result;
    } ///< No physical controller SDK is needed for semantic replay.

    element_handle root = tree.create(); ///< Session-local canvas identity.
};
} // namespace atlas::ui::tests
