/** @file @brief Increasing input frames and ordered serial event/work boundaries. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::ui
{
/// @brief All potentially throwing geometry preparation precedes command-frame admission.
void input_session::begin_frame(std::uint64_t frame, const element_snapshot& source,
                                const layout_snapshot& geometry, float delta_seconds)
{
    impl_->check();
    if (impl_->terminal || (!impl_->stats.closed && !impl_->stats.failed))
    {
        throw std::logic_error("end the current UI input frame before beginning another");
    }

    if (!frame || frame <= impl_->stats.frame || source.session() != impl_->session ||
        !std::isfinite(delta_seconds) || delta_seconds < 0)
    {
        throw std::invalid_argument("invalid UI input frame, source or delta");
    }

    if (source.elements().size() > impl_->limits.max_nodes)
    {
        throw std::overflow_error("UI input presentation node limit exhausted");
    }

    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.input.prepare", "ui.cpu"}};
    std::pmr::vector<detail::presentation_region> prepared(0, impl_->memory.get());
    detail::paint_preparation unused{impl_->memory.get()};
    detail::prepare_paint(source, geometry, impl_->memory.get(), {}, unused, &prepared, false);
    std::size_t count = 0;
    for (const auto& value : prepared)
    {
        count = std::max(count, std::size_t{value.target.index} + 1);
    }

    std::pmr::vector<std::size_t> slots(count, prepared.size(), impl_->memory.get());
    std::pmr::vector<std::size_t> depths(count, 0, impl_->memory.get());
    for (std::size_t i = 0; i < prepared.size(); ++i)
    {
        const auto& value = prepared[i];
        slots[value.target.index] = i;
        const auto depth = value.parent.generation != 0 ? depths[value.parent.index] + 1 : 1;
        depths[value.target.index] = depth;
        const auto* binding = impl_->find(value.target);
        if (binding && !binding->retiring && !binding->ended &&
            depth > impl_->limits.max_route_depth)
        {
            throw std::overflow_error("UI input route depth limit exhausted");
        }
    }

    std::unique_ptr<scroll_input_frame> scrolls;
    if (!geometry.scrolls().empty() || !geometry.scrollbars().empty())
    {
        scrolls = std::make_unique<scroll_input_frame>(source, geometry, impl_->memory.get());
    }

    impl_->tree.commands().begin_frame(frame);
    auto previous_scrolls = std::move(impl_->scrolling);
    impl_->scrolling = std::move(scrolls);
    impl_->regions.swap(prepared);
    impl_->slots.swap(slots);
    impl_->stats = {frame, 0, 0, false, false};
    impl_->delta = delta_seconds;
    impl_->stage = 1;
    try
    {
        impl_->reconcile();
        if (previous_scrolls)
        {
            impl_->preserve_scroll_anchor(*previous_scrolls);
        }

        std::erase_if(impl_->bindings,
                      [](const auto& value)
                      {
                          return value.ended;
                      });
        impl_->start_bindings();
        impl_->reconcile();
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}

/// @brief Lifecycle starts only after all new-frame allocations and scroll preparation succeed.
void input_session::impl::start_bindings()
{
    for (const auto& value : regions)
    {
        auto* binding = find(value.target);
        if (!binding || !eligible(value.target))
        {
            continue;
        }

        if (!binding->started)
        {
            binding->started = true;
            (void)send(value.target, event_kind::start, route_phase::direct, value.target);
        }

        if (!binding->enabled && eligible(value.target))
        {
            binding->enabled = true;
            (void)send(value.target, event_kind::enable, route_phase::direct, value.target);
        }
    }
}

/// @brief Whole-batch preflight rejects invalid replay before process-input or raw callbacks.
void input_session::process(std::span<const input_event> events)
{
    process_packets(events, false);
}

/// @brief Cross-view routing preserves packet order without broadcasting process-input twice.
void input_session::process_more(std::span<const input_event> events)
{
    process_packets(events, true);
}

/// @brief Preflight each admitted chunk before callbacks and charge the cumulative frame budget.
void input_session::process_packets(std::span<const input_event> events, bool append)
{
    impl_->open();
    if (impl_->stage != (append ? 2U : 1U))
    {
        throw std::logic_error("UI input is processed once before tick");
    }

    if (events.size() > impl_->limits.max_events - impl_->stats.events)
    {
        throw std::overflow_error("UI input packet limit exhausted");
    }

    validate_input_events(events);

    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.input.process", "ui.dispatch"}};
    try
    {
        impl_->stage = 2;
        if (!append)
        {
            impl_->broadcast(event_kind::process_input);
        }

        for (const auto& value : events)
        {
            ++impl_->stats.events;
            impl_->process_one(value);
        }
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}

/// @brief Shared shape validation prevents a malformed suffix from admitting a navigation prefix.
void validate_input_events(std::span<const input_event> events)
{
    for (const auto& value : events)
    {
        if (value.kind > input_kind::pointer_scroll || !std::isfinite(value.position.x) ||
            !std::isfinite(value.position.y) || !std::isfinite(value.scroll.x) ||
            !std::isfinite(value.scroll.y) ||
            (value.kind != input_kind::pointer_scroll && value.scroll != layout_point{}) ||
            (value.kind == input_kind::action) != value.action.has_value() ||
            (value.action &&
             (!value.action->action || !value.action->frame ||
              value.action->type > input::value_type::axis2d ||
              value.action->phase > input::action_phase::cancelled ||
              !std::isfinite(value.action->value.x) || !std::isfinite(value.action->value.y))))
        {
            throw std::invalid_argument("invalid UI input packet");
        }
    }
}

/// @brief Caller-owned job phases may run and join between these serial dispatch methods.
void input_session::impl::system_stage(event_kind kind)
{
    open();
    const unsigned expected = kind == event_kind::tick        ? 2U
                              : kind == event_kind::late_tick ? 3U
                                                              : 4U;
    const unsigned next = expected == 4 ? 0U : expected + 1;
    if (stage != expected)
    {
        throw std::logic_error("UI system event stages are out of order");
    }

    const guard lock{busy};
    const diagnostics::operation_scope scope{destinations, {"ui.input.system", "ui.dispatch"}};
    try
    {
        stage = next;
        broadcast(kind);
        if (!next)
        {
            flush_scroll();
            stats.closed = true;
        }
    }
    catch (...)
    {
        fail();
        throw;
    }
}

/// @brief Tick cannot precede input processing, including an empty input batch.
void input_session::tick()
{
    impl_->system_stage(event_kind::tick);
}

/// @brief Late-tick follows tick and the caller's chosen synchronization point.
void input_session::late_tick()
{
    impl_->system_stage(event_kind::late_tick);
}

/// @brief End-frame production does not implicitly seal or apply consumer commands.
void input_session::end_frame()
{
    impl_->system_stage(event_kind::end_frame);
}

} // namespace atlas::ui
