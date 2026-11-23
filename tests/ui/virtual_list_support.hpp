/** @file @brief Isolated bounded virtual-list ownership and explicit input boundaries. */
#pragma once
#include "input_test_support.hpp"
#include "layout_test_support.hpp"
#include <atlas/ui/virtual_list.hpp>

namespace atlas::ui::tests
{
/// @brief Fresh dispatcher/tree/input/controller and deterministic data per test.
class virtual_list_fixture : public input_fixture
{
  protected:
    void SetUp() override
    {
        viewport = tree.create();
        tree.set_panel(viewport, scroll_layout{});
        tree.set_viewport({0, 0, 100, 80});
        list = std::make_unique<virtual_list>(
            tree, input, viewport, workers.dispatcher(),
            [this](row_context& context)
            {
                if (building)
                {
                    building(context);
                }

                auto paint_style = context.tree().paint(context.root());
                paint_style.fill = true;
                context.tree().set_paint(context.root(), paint_style);
            });
    } ///< No external project assets or random runtime state.

    void update(std::size_t count = 100)
    {
        data = std::make_shared<fixed_item_source>(fixed_item_options{count, 20});
        list->update(data, 80);
    } ///< Exact fixed reference rows.

    void scroll(float position)
    {
        auto panel = std::get<scroll_layout>(tree.panel(viewport));
        panel.offset.y = position;
        tree.set_panel(viewport, panel);
        list->update(data, 80);
    } ///< Emulate a committed #121 wheel/drag offset at a frame boundary.

    void open()
    {
        begin(++frame);
        list->restore_focus();
    } ///< Realize before capturing input, then restore logical focus.

    /// @brief Observe the committed scroll offset independently of presentation snapshots.
    float position() const
    {
        return std::get<scroll_layout>(tree.panel(viewport)).offset.y;
    }

    ///< Observe authored clamped offset after publication.

    element_handle viewport;                    ///< Shared scroll owner.
    std::shared_ptr<const item_source> data;    ///< Immutable logical source.
    std::function<void(row_context&)> building; ///< Controlled success/failure factory.
    std::unique_ptr<virtual_list> list;         ///< Destroy before borrowed tree and input.
    std::uint64_t frame = 0;                    ///< Increasing input identity.
};
} // namespace atlas::ui::tests
