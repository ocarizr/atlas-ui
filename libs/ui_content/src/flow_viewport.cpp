/** @file @brief Parent-first mapped layout and closed owner phases for composed Flow UI. */
#include "flow_viewport_state.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui_content
{
namespace
{
/// @brief Reject invalid deltas before integral animation-clock conversion can overflow.
animation_time elapsed(float seconds)
{
    const auto nanos = static_cast<long double>(seconds) * 1'000'000'000.L;
    if (!std::isfinite(seconds) || seconds < 0 ||
        nanos >= static_cast<long double>(animation_time::max().count()))
    {
        throw std::invalid_argument("Invalid Flow frame/animation delta");
    }

    return animation_time{static_cast<animation_time::rep>(nanos)};
}
} // namespace

/// @brief Reuse application engines and optional diagnostics on the input owner.
flow_viewport::flow_viewport(flow_manager& flow, ui::layout_engine& layout, ui::paint_engine& paint,
                             diagnostics::services sinks)
    : impl_{std::make_unique<impl>(flow, layout, paint, std::move(sinks))}
{
}

flow_viewport::~flow_viewport() = default; ///< No borrowed presentation survives a frame.

/// @brief Invisible hosts suspend input without destroying the submenu's independent model.
void flow_viewport::impl::activate(layer& entry, std::uint64_t frame, float seconds)
{
    auto& view = *entry.item.view;
    if (!entry.item.input)
    {
        if (input_sessions.erase(entry.source.session()) && view.input().statistics().frame < frame)
        {
            const auto focused = view.input().focused();
            if (focused.generation)
            {
                bookmarks[std::string{entry.item.screen}] = view.authored_id(focused);
            }

            view.begin_models(frame);
            view.input().suspend(frame);
            view.synchronize();
            view.tree().apply(view.tree().commands().seal());
        }

        return;
    }

    input_sessions.insert(entry.source.session());
    view.begin_models(frame);
    view.input().begin_frame(frame, entry.source, entry.geometry, seconds);
    view.synchronize();
    flow.restore_focus(view);
    view.input().process({});
}

/// @brief Open matching owner frames, then choose the frontmost eligible keyboard scope.
void flow_viewport::impl::begin(std::uint64_t frame, float seconds)
{
    const auto delta = elapsed(seconds);
    std::erase_if(input_sessions,
                  [&](auto id)
                  {
                      return std::ranges::none_of(layers,
                                                  [&](const auto& entry)
                                                  {
                                                      return entry.source.session() == id;
                                                  });
                  });
    for (auto& entry : layers)
    {
        entry.item.view->advance_animation(delta);
        activate(entry, frame, seconds);
    }

    const auto top = std::find_if(layers.rbegin(), layers.rend(),
                                  [](const auto& entry)
                                  {
                                      return entry.item.input;
                                  });
    if (top == layers.rend())
    {
        keyboard.clear();
        front.clear();
        return;
    }

    const bool new_front = front != top->item.screen;
    front = top->item.screen;
    const auto selected =
        std::ranges::find_if(layers,
                             [&](const auto& entry)
                             {
                                 return entry.item.input && entry.item.screen == keyboard;
                             });
    const auto last = static_cast<std::size_t>(std::distance(layers.begin(), top.base()) - 1);
    focus(new_front || selected == layers.end()
              ? last
              : static_cast<std::size_t>(selected - layers.begin()));
}

/// @brief Complete every serial producer before publishing tree commands.
void flow_viewport::impl::finish()
{
    for (auto& layer : layers)
    {
        if (!layer.item.input)
        {
            continue;
        }

        auto& view = *layer.item.view;
        view.input().tick();
        view.synchronize();
        view.input().late_tick();
        view.input().end_frame();
        view.synchronize();
        view.publish_animation();
        view.tree().apply(view.tree().commands().seal());
    }
}

/// @brief Finish every opened input session even if a script or action fails.
std::vector<ui::paint_quad> flow_viewport::frame(std::uint64_t frame, ui::layout_rect bounds,
                                                 std::span<const ui::input_event> events,
                                                 flow_frame_options options)
{
    const diagnostics::operation_scope scope{impl_->sinks, {"ui.flow.viewport", "ui.content"}};
    if (events.size() > 256)
    {
        throw std::length_error("Flow replay/input event budget exhausted");
    }

    ui::validate_input_events(events);

    impl_->arrange(bounds);
    impl_->viewport = bounds;
    try
    {
        impl_->begin(frame, options.delta_seconds);
        for (const auto& event : events)
        {
            impl_->route(event, options.back_action);
        }

        impl_->finish();
    }
    catch (...)
    {
        for (auto& layer : impl_->layers)
        {
            auto& view = *layer.item.view;
            if (!view.input().statistics().closed)
            {
                try
                {
                    view.input().suspend(frame + 1);
                    view.tree().apply(view.tree().commands().seal());
                }
                catch (...)
                {
                    diagnostics::write(impl_->sinks, diagnostics::log_level::warning,
                                       "ui.flow.viewport",
                                       "Input cleanup failed while preserving the original error");
                }
            }
        }

        throw;
    }

    impl_->arrange(bounds);
    std::vector<ui::paint_quad> result;
    for (const auto& layer : impl_->layers)
    {
        const auto packet = impl_->paint.calculate(layer.source, layer.geometry);
        for (auto quad : packet.quads())
        {
            quad.clip = ui::intersect_clip(quad.clip, layer.bounds);
            if (quad.clip.width <= 0 || quad.clip.height <= 0)
            {
                continue;
            }

            for (auto& vertex : quad.vertices)
            {
                vertex.color[3] *= layer.opacity;
            }

            result.push_back(quad);
        }
    }

    impl_->layers.clear();
    return result;
}
} // namespace atlas::ui_content
