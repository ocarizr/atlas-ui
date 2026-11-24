/** @file @brief Visible-range admission, logical anchor preservation and joined immutable work. */
#include "virtual_list_state.hpp"
#include <cmath>

namespace atlas::ui
{
/// @brief Preserve logical anchors, resolve requested focus and clamp the new source range.
float virtual_list::impl::offset(const item_source& source, float height) const
{
    const auto old = navigation->source;
    auto position = std::get<scroll_layout>(tree.panel(viewport)).offset.y;
    const auto range = std::max(0.0F, source.extent() - height);
    if (old && old.get() != &source && old->count() && position == previous_offset)
    {
        const auto anchor_index = std::min(old->locate(previous_offset), old->count() - 1);
        const auto anchor = old->item(anchor_index);
        const auto replacement = source.find(anchor.key);
        if (source.count())
        {
            const auto index = replacement.value_or(std::min(anchor_index, source.count() - 1));
            position =
                source.item(index).offset + (replacement ? previous_offset - anchor.offset : 0);
        }

        if (previous_offset > 0 &&
            previous_offset == std::max(0.0F, old->extent() - previous_height))
        {
            position = range;
        }
    }

    if (navigation->pending)
    {
        auto found = source.find(*navigation->pending);
        if (!found)
        {
            const auto previous = old ? old->find(*navigation->pending) : std::nullopt;
            if (!previous)
            {
                throw std::invalid_argument("logical focus item is absent from source");
            }

            if (!source.count())
            {
                return 0;
            }

            found = std::min(*previous, source.count() - 1);
        }

        const auto target = source.item(*found);
        if (*found == 0 || target.height > height || target.offset < position)
        {
            position = target.offset;
        }
        else if (*found + 1 == source.count())
        {
            position = range;
        }
        else if (target.offset + target.height > position + height)
        {
            position = target.offset + target.height - height;
        }
    }

    return std::clamp(position, 0.0F, range);
}

namespace
{
/// @brief Borrowed immutable source outlives the joined phase; no row/tree ownership crosses jobs.
struct item_request
{
    const item_source* source = nullptr; ///< Stable source retained by publish until join.
    std::size_t index = 0;               ///< Bounded visible/overscan index.
};

/// @brief Exclusive numeric outputs with cancellation checkpoints and no live UI access.
void describe_rows(const phase_range<item_request, realized_row>& range)
{
    for (std::size_t i = 0; i < range.inputs.size(); ++i)
    {
        range.throw_if_cancelled();
        const auto& request = range.inputs[i];
        range.outputs[i] = {request.index, request.source->item(request.index), {}};
    }
}
} // namespace

/// @brief Admit a bounded visible range, join metadata work and publish tree and handlers together.
void virtual_list::impl::publish(std::shared_ptr<const item_source> source, float height,
                                 const jobs::job_options& options)
{
    detail::input_edit::require_idle(input);
    if (options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    const auto position = offset(*source, height);
    auto first = source->locate(position);
    if (first > source->count())
    {
        throw std::invalid_argument("invalid virtual source range");
    }

    auto last = first;
    while (height > 0 && last < source->count() && source->item(last).offset < position + height)
    {
        if (last - first >= limits.max_realized)
        {
            throw std::overflow_error("visible virtual rows exceed admission limit");
        }

        ++last;
    }

    first -= std::min(first, limits.overscan);
    last += std::min(source->count() - last, limits.overscan);
    if (last - first > limits.max_realized)
    {
        throw std::overflow_error("virtual rows plus overscan exceed admission limit");
    }

    const auto policy = std::get<scroll_layout>(tree.panel(viewport));
    if (source == navigation->source && height == previous_height && policy.offset.y == position &&
        rows.size() == last - first && (rows.empty() || rows.front().index == first))
    {
        if (navigation->pending)
        {
            navigation->focused = *navigation->pending;
        }

        navigation->pending.reset();
        stats.created = 0;
        stats.reused = rows.size();
        stats.admitted_jobs = 0;
        previous_offset = position;
        return;
    }

    std::pmr::vector<item_request> requests(0, memory.get());
    requests.reserve(last - first);
    for (auto i = first; i < last; ++i)
    {
        requests.push_back({source.get(), i});
    }

    if (frame == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("virtual metadata frame exhausted");
    }

    work.begin_frame(++frame, options);
    const auto described = work.run_phase<realized_row>(
        "virtual.rows", std::span<const item_request>{requests}, describe_rows);
    work.end_frame();
    std::pmr::vector<realized_row> prepared(described.values().begin(), described.values().end(),
                                            memory.get());
    commit_rows(std::move(source), {position, height}, prepared, options);
}

/// @brief Publish completely staged bindings and rows, retaining source and focus on any failure.
void virtual_list::impl::commit_rows(std::shared_ptr<const item_source> source, virtual_window view,
                                     std::pmr::vector<realized_row>& prepared,
                                     const jobs::job_options& options)
{
    std::pmr::vector<std::shared_ptr<detail::row_lease>> retained(0, memory.get());
    retained.reserve(prepared.size());
    auto focus = navigation->pending.value_or(navigation->focused);
    if (focus && !source->find(focus))
    {
        const auto old_index = navigation->source ? navigation->source->find(focus).value_or(0) : 0;
        focus = source->count() ? source->item(std::min(old_index, source->count() - 1)).key : 0;
    }

    detail::input_edit bindings{input};
    detail::tree_edit edit{tree};
    const auto candidate_content = stage(edit, bindings, prepared, source->extent(), retained);
    auto panel = std::get<scroll_layout>(edit.draft().panel(edit.staged(viewport)));
    panel.offset.y = view.offset;
    edit.draft().set_panel(edit.staged(viewport), panel);
    if (options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    edit.commit();
    bindings.commit();
    content = candidate_content;
    stats = {source->count(),
             prepared.size(),
             0,
             0,
             work.statistics().admitted_jobs,
             prepared.capacity() * sizeof(realized_row) +
                 retained.capacity() * sizeof(leases.front())};
    for (const auto& row : prepared)
    {
        const bool reused = std::any_of(rows.begin(), rows.end(),
                                        [&](const auto& old)
                                        {
                                            return old.ticket == row.ticket;
                                        });
        if (reused)
        {
            ++stats.reused;
        }
        else
        {
            ++stats.created;
        }
    }

    rows.swap(prepared);
    leases.swap(retained);
    navigation->source = std::move(source);
    navigation->focused = focus;
    navigation->pending.reset();
    previous_offset = view.offset;
    previous_height = view.height;
}
} // namespace atlas::ui
