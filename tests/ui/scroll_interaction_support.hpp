/** @file @brief Shared list, replay and external chrome fixtures for scroll interaction contexts.
 */
#pragma once
#include "scroll_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Explicit list dimensions avoid interchangeable numeric fixture arguments.
struct scroll_list_options
{
    unsigned count = 5; ///< Number of independently focusable list items.
    float height = 40;  ///< Each item's natural logical height.
};

/// @brief Every fact owns a list, semantic handlers, input frame sequence and chrome.
class scroll_navigation_fixture : public scroll_fixture
{
  protected:
    void list(scroll_list_options options = {})
    {
        tree.set_panel(content, vertical_layout{});
        resize_content({});
        for (unsigned index = 0; index < options.count; ++index)
        {
            const auto item = rectangle(content);
            auto style = tree.layout(item);
            style.intrinsic = {100, options.height};
            tree.set_layout(item, style);
            input.bind(item, handler, {.focusable = true});
            items.push_back(item);
        }
    } ///< Natural item geometry and no application-specific input dependency.

    void open()
    {
        begin(frame++);
    } ///< Freeze a coherent source/layout packet for this input frame.

    void replay(std::initializer_list<input_event> events = {})
    {
        input.process({events.begin(), events.size()});
        finish();
    } ///< Queue, finish and publish through the existing scalar command boundary.

    input_event wheel(float y, float x = 0) const
    {
        input_event result{input_kind::pointer_scroll, {20, 20}};
        result.scroll = {x, y};
        return result;
    } ///< Positive notches advance toward the trailing boundary.

    float position() const
    {
        return std::get<scroll_layout>(tree.panel(viewport)).offset.y;
    } ///< Read committed authored state after the frame publication.

    element_handle chrome(scrollbar_style style = {})
    {
        const auto bar = rectangle();
        layout_style shape;
        shape.constraints.minimum = {10, 80};
        shape.constraints.maximum = {10, 80};
        shape.horizontal = layout_alignment::end;
        tree.set_layout(bar, shape);
        style.viewport = viewport;
        tree.set_scrollbar(bar, style);
        return bar;
    } ///< Independent overlay chrome does not become scroll content.

    std::vector<element_handle> items; ///< Stable item identities, local to this fact.
    std::uint64_t frame = 1;           ///< Strictly increasing input frame identity.
};
} // namespace atlas::ui::tests
