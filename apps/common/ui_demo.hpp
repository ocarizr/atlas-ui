/** @file @brief Generic main-thread UI demonstration shared by editor and runtime. */
#pragma once
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_images/image_renderer.hpp>

namespace atlas::application
{
class ui_demo
{
  public:
    ui_demo(const jobs::job_dispatcher& dispatcher,
            std::shared_ptr<std::pmr::memory_resource> memory, diagnostics::services destinations,
            ui_images::image_cache& images, std::uint64_t key)
        : images_(images), key_(key), tree_({}, memory, destinations),
          layout_(dispatcher, {}, memory, destinations),
          paint_(dispatcher, {}, std::move(memory), std::move(destinations))
    {
        const auto canvas = tree_.create();
        tree_.set_panel(canvas, ui::canvas_layout{});
        for (unsigned column = 0; column < 3; ++column)
        {
            const auto panel = tree_.create(canvas);
            tree_.set_panel(panel, ui::vertical_layout{10});
            ui::layout_style style;
            style.padding = {12, 12, 12, 12};
            tree_.set_layout(panel, style);
            ui::canvas_slot slot;
            slot.horizontal = {
                static_cast<float>(column) / 3, static_cast<float>(column + 1) / 3, 12, 12, 0,
                ui::canvas_sizing::stretch};
            slot.vertical = {0, 1, 20, 20, 0, ui::canvas_sizing::stretch};
            tree_.set_canvas_slot(panel, slot);
            ui::paint_style appearance;
            appearance.fill = true;
            appearance.clip = true;
            appearance.color = {0.09f, 0.12f, 0.18f, 1};
            tree_.set_paint(panel, appearance);
            for (unsigned row = 0; row < 9; ++row)
            {
                const auto line = tree_.create(panel);
                tree_.set_panel(line, ui::horizontal_layout{8});
                style = {};
                style.constraints.minimum.height = 64;
                style.constraints.maximum.height = 64;
                tree_.set_layout(line, style);
                const auto image = tree_.create(line);
                style.constraints.minimum.width = 64;
                style.constraints.maximum.width = 64;
                tree_.set_layout(image, style);
                appearance = {};
                appearance.fill = true;
                appearance.texture = key_;
                appearance.image = ui::image_sampling{
                    {32, 32}, row % 2 == 0 ? ui::image_fit::contain : ui::image_fit::cover};
                image_handles_.push_back(image);
                appearance.color = {1, 1, 1, 0.9f};
                tree_.set_paint(image, appearance);
                const auto block = tree_.create(line);
                style.constraints.minimum.width = 120;
                style.constraints.maximum.width = 120;
                tree_.set_layout(block, style);
                appearance.texture = 0;
                appearance.image.reset();
                appearance.color = {0.15f + static_cast<float>(column) * 0.12f,
                                    0.3f + static_cast<float>(row) * 0.04f, 0.65f, 0.8f};
                tree_.set_paint(block, appearance);
            }
        }
    } ///< Author clipped panels and textured rows using shared runtime contracts.

    std::shared_ptr<const render_host::frame_payload> snapshot(const window::window_state& window)
    {
        const ui::layout_rect viewport{0, 0, static_cast<float>(window.width),
                                       static_cast<float>(window.height)};
        images_.poll();
        const auto images = images_.snapshot();
        const auto* image = images.find(key_);
        const auto status = image ? image->status : ui_images::image_status::cancelled;
        if (!last_ || viewport != viewport_ || status != last_status_)
        {
            for (const auto target : image_handles_)
            {
                auto style = tree_.layout(target);
                style.intrinsic =
                    image ? ui_images::intrinsic_size(*image) : ui::layout_extent{32, 32};
                tree_.set_layout(target, style);
                auto appearance = tree_.paint(target);
                if (appearance.image)
                {
                    appearance.image->source = style.intrinsic;
                }

                tree_.set_paint(target, appearance);
            }

            tree_.set_viewport(viewport);
            const auto input = tree_.snapshot();
            auto result = paint_.calculate(input, layout_.calculate(input));
            last_ = std::make_shared<const ui_images::frame_payload>(std::move(result), images);
            viewport_ = viewport;
            last_status_ = status;
        }

        return last_;
    } ///< Main owns authoring; render receives only retained immutable CPU values.
  private:
    ui_images::image_cache& images_; ///< Main-thread nonblocking image state.
    std::uint64_t key_;              ///< One shared image identity used by all thumbnail widgets.
    std::vector<ui::element_handle> image_handles_; ///< Bound leaf elements, not resource owners.
    ui_images::image_status last_status_ =
        ui_images::image_status::pending;                  ///< Last published state.
    ui::element_tree tree_;                                ///< Main-thread authoring state.
    ui::layout_engine layout_;                             ///< Shared-worker layout coordinator.
    ui::paint_engine paint_;                               ///< Shared-worker paint coordinator.
    ui::layout_rect viewport_;                             ///< Last source extent.
    std::shared_ptr<const ui_images::frame_payload> last_; ///< Reused until viewport/image changes.
}; ///< Technology demonstration without game-specific behavior.
} // namespace atlas::application
