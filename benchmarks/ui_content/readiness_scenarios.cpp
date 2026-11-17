/** @file @brief Viewport documents and deterministic editing and playback inputs. */
#include "readiness_scenarios.hpp"

namespace atlas::ui_content::readiness
{
using namespace std::chrono_literals;

/// @brief Keep the same ordered hierarchy and asset identities for both policies.
document make_document(scenario shape, const scenario_assets& dependencies)
{
    document source;
    source.models.push_back({"frame", dependencies.model});
    source.elements.reserve(1 + shape.panels * (1 + shape.rows));
    auto& root = source.elements.emplace_back();
    root.id = "canvas";
    root.panel = ui::canvas_layout{};
    root.script = behavior{dependencies.script, "frame"};
    for (std::size_t panel = 0; panel < shape.panels; ++panel)
    {
        const auto id = "panel_" + std::to_string(panel);
        auto& stack = source.elements.emplace_back();
        stack.id = id;
        stack.parent = "canvas";
        stack.panel = ui::vertical_layout{1};
        stack.requested.width = 188.0F;
        stack.requested.height = 960.0F;
        stack.requested.width_fixed = true;
        stack.requested.height_fixed = true;
        stack.paint.clip = true;
        const auto column = panel % 9;
        const auto lane = panel / 9;
        stack.canvas =
            ui::canvas_slot{.horizontal = {.offset_min = static_cast<float>(column) * 204.0F},
                            .vertical = {.offset_min = static_cast<float>(lane) * 12.0F}};
        for (std::size_t row = 0; row < shape.rows; ++row)
        {
            auto& card = source.elements.emplace_back();
            card.id = id + "_row_" + std::to_string(row);
            card.parent = id;
            card.caption = card.id;
            card.requested.height = 14.0F;
            card.requested.width = 176.0F;
            card.requested.width_fixed = true;
            card.requested.height_fixed = true;
            card.paint.fill = true;
            card.paint.color = {0.25F, 0.5F, 0.75F, 0.9F};
            card.material = dependencies.material;
        }
    }

    return source;
}

/// @brief Warm the same animation on each isolated composition.
void start(instance& view, update change)
{
    switch (change)
    {
        case update::material:
            view.play_animation(
                {800ms, {{"panel_0_row_0", ui::property::material_glow, {{0ms, 0}, {800ms, 1}}}}},
                animation_repeat::loop);
            break;
        case update::drawer:
            view.play_animation(
                {800ms, {{"panel_0", ui::property::translate_x, {{0ms, 0}, {800ms, -180}}}}},
                animation_repeat::loop);
            break;
        case update::reflow:
            view.play_animation(
                {800ms, {{"panel_0", ui::property::width, {{0ms, 188}, {800ms, 120}}}}},
                animation_repeat::loop);
            break;
        case update::idle:
        case update::resize:
            break;
    }
}

/// @brief Run the owner lifecycle and publish model and animation effects.
void dispatch(instance& view, const ui::layout_snapshot& input_geometry, std::uint64_t frame)
{
    view.advance_animation(16ms);
    view.begin_models(frame);
    auto& tree = view.tree();
    auto& input = view.input();
    input.begin_frame(frame, tree.snapshot(), input_geometry);
    input.process({});
    input.tick();
    input.late_tick();
    input.end_frame();
    view.synchronize();
    view.publish_animation();
    tree.apply(tree.commands().seal());
}
} // namespace atlas::ui_content::readiness
