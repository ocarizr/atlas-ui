/** @file @brief Generic main-thread UI demonstration shared by editor and runtime. */
#pragma once
#include "demo_interaction.hpp"
#include "input_adapter.hpp"
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_images/image_renderer.hpp>
#include <chrono>

namespace atlas::application
{
class ui_demo
{
  public:
    ui_demo(const jobs::job_dispatcher& dispatcher,
            std::shared_ptr<std::pmr::memory_resource> memory, diagnostics::services destinations,
            ui_images::image_cache& images, std::uint64_t key)
        : images_(images), key_(key), tree_({}, memory, destinations),
          actions_({}, memory, destinations),
          input_(tree_, {.max_events = 4096}, memory, destinations), routed_(memory.get()),
          layout_(dispatcher, {}, memory, destinations),
          paint_(dispatcher, {}, std::move(memory), std::move(destinations))
    {
        actions_.define_action({1, "confirm"});
        actions_.define_action({2, "cancel"});
        const std::array bindings{
            input::binding{
                1, {input::device_kind::keyboard, static_cast<std::uint32_t>(input::key::enter)}},
            input::binding{
                1, {input::device_kind::keyboard, static_cast<std::uint32_t>(input::key::space)}},
            input::binding{
                2, {input::device_kind::keyboard, static_cast<std::uint32_t>(input::key::escape)}}};
        actions_.define_context(
            {1, "viewport", 0, true, input::activation_policy::require_neutral, bindings});
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
                input_.bind(image, std::make_shared<demo_interaction>(), {.focusable = true});
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

    std::shared_ptr<const render_host::frame_payload> snapshot(const window::window_state& window,
                                                               std::span<const input::event> events)
    {
        const ui::layout_rect viewport{0, 0, static_cast<float>(window.width),
                                       static_cast<float>(window.height)};
        images_.poll();
        const auto images = images_.snapshot();
        const auto* image = images.find(key_);
        const auto status = image ? image->status : ui_images::image_status::cancelled;
        const bool geometry_changed = !last_ || viewport != viewport_ || status != last_status_;
        if (geometry_changed)
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
            source_ = tree_.snapshot();
            geometry_ = layout_.calculate(source_);
        }

        const auto now = std::chrono::steady_clock::now();
        const auto delta = std::chrono::duration<float>(now - previous_tick_).count();
        previous_tick_ = now;
        input_.begin_frame(++frame_, source_, geometry_, delta);
        input_time_ += delta;
        routed_.clear();
        actions_.dispatch(frame_, input_time_, events, routed_);
        input_.process(routed_.values());
        input_.tick();
        input_.late_tick();
        input_.end_frame();
        const bool appearance_changed = tree_.commands().pending() != 0;
        tree_.apply(tree_.commands().seal());
        if (appearance_changed)
        {
            source_ = tree_.snapshot();
        }

        if (geometry_changed || appearance_changed)
        {
            auto result = paint_.calculate(source_, geometry_);
            last_ = std::make_shared<const ui_images::frame_payload>(std::move(result), images);
            viewport_ = viewport;
            last_status_ = status;
        }

        return last_;
    } ///< Main owns authoring; render receives only retained immutable CPU values.

    void shutdown()
    {
        tree_.commands().discard_pending();
        input_.shutdown(++frame_);
        tree_.apply(tree_.commands().seal());
    } ///< Deliver ordered final lifecycle while the tree and its command producer remain alive.
  private:
    ui_images::image_cache& images_; ///< Main-thread nonblocking image state.
    std::uint64_t key_;              ///< One shared image identity used by all thumbnail widgets.
    std::vector<ui::element_handle> image_handles_; ///< Bound leaf elements, not resource owners.
    ui_images::image_status last_status_ =
        ui_images::image_status::pending; ///< Last published state.
    ui::element_tree tree_;               ///< Main-thread authoring state.
    input::input_system actions_;         ///< Independent semantic action mapping owner.
    double input_time_ = 0;               ///< Caller clock for nonwaiting trigger recognition.
    ui::input_session input_;      ///< Serial routing; never dispatch from recording workers.
    ui_input_batch routed_;        ///< PMR scratch reused across owner frames.
    ui::element_snapshot source_;  ///< Current immutable presentation inputs.
    ui::layout_snapshot geometry_; ///< Geometry reused across appearance-only input commands.
    /// @brief Monotonic time used for serial lifecycle deltas.
    std::chrono::steady_clock::time_point previous_tick_ = std::chrono::steady_clock::now();
    ///< Monotonic delta for owner-thread lifecycle production.
    std::uint64_t frame_ =
        0; ///< Input production frames are independent of render queue admission.
    ui::layout_engine layout_; ///< Shared-worker layout coordinator.
    ui::paint_engine paint_;   ///< Shared-worker paint coordinator.
    ui::layout_rect viewport_; ///< Last source extent.
    std::shared_ptr<const ui_images::frame_payload>
        last_; ///< Reused while presentation is unchanged.
}; ///< Technology demonstration without game-specific behavior.
} // namespace atlas::application
