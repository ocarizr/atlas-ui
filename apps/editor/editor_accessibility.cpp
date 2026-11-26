/** @file @brief Native-free editor accessibility attachment and owner text resolution. */
#include "editor_view_state.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Attach the active window channel and an owner-only text resolver.
void editor_view::bind_accessibility(std::shared_ptr<accessibility::channel> source,
                                     ui::accessible_text resolve)
{
    impl_->accessibility_channel = std::move(source);
    impl_->focus_owner = 0;
    impl_->accessible_text = resolve ? std::move(resolve) : [](std::string_view key)
    {
        return std::string{key};
    };
}

/// @brief Refresh physical screen origin and scale independently of viewport resizing.
void editor_view::accessibility_coordinates(ui::accessibility_view coordinates)
{
    impl_->accessibility_coordinates = coordinates;
}

/// @brief Update layout for window changes, then route input and publish any resulting changes.
std::shared_ptr<const editor_frame> editor_view::snapshot(const window::window_state& state,
                                                          std::span<const input::event> events)
{
    const auto density = std::clamp(state.dpi_scale, 0.5f, 8.f);
    const ui::layout_rect bounds{0, 0, static_cast<float>(state.width) / density,
                                 static_cast<float>(state.height) / density};
    if (impl_->assets && impl_->assets->poll(impl_->workspace, impl_->jobs, impl_->sinks))
    {
        impl_->chrome.reset();
    }

    if (!impl_->chrome || bounds != impl_->viewport || density != impl_->density)
    {
        impl_->rebuild(bounds, density);
    }

    std::optional<ui::accessibility_frame> accessibility;
    if (impl_->accessibility_channel)
    {
        accessibility.emplace(*impl_->accessibility_channel, impl_->accessibility_coordinates,
                              impl_->focus_owner);
    }

    auto* frame = accessibility ? &*accessibility : nullptr;
    const bool editing = impl_->accessibility_fields.input(events, impl_->session());
    std::vector<input::event> filtered;
    if (editing)
    {
        for (const auto& event : events)
        {
            if (event.device.kind != input::device_kind::keyboard ||
                event.kind == input::event_kind::focus_lost ||
                event.kind == input::event_kind::source_reset)
            {
                filtered.push_back(event);
            }
        }

        events = filtered;
    }

    const bool preview_changed =
        impl_->flow && impl_->flow->pump_preview(events, density, frame, impl_->accessible_text);
    const auto checkpoint = frame ? frame->checkpoint() : 0;
    const bool changed = (!events.empty() || frame) && impl_->dispatch(events, frame);
    if (changed || preview_changed || editing)
    {
        if (frame)
        {
            impl_->focus_owner = frame->focus_owner();
        }

        impl_->rebuild(bounds, density);
        if (frame)
        {
            frame->rollback(checkpoint);
            frame->focus_owner(impl_->focus_owner);
            (void)impl_->chrome->begin_input({});
            frame->route(impl_->chrome->input());
            frame->capture(impl_->chrome->input(), impl_->accessible_text);
            (void)impl_->chrome->finish_input();
        }
    }

    if (frame)
    {
        frame->publish();
        impl_->focus_owner = frame->focus_owner();
    }

    return impl_->presentation();
}
} // namespace atlas::editor
