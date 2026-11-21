/** @file @brief Bounded view transforms and one-history-entry drag transactions. */
#include "document_state.hpp"
#include <algorithm>
#include <atlas/editor_flow/workspace.hpp>
#include <cmath>
#include <utility>

namespace atlas::editor_flow
{
namespace
{
/// @brief Reject nonfinite transform input before mutating any gesture state.
void finite(point value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || std::abs(value.x) > 1000000 ||
        std::abs(value.y) > 1000000)
    {
        throw std::invalid_argument("Invalid Flow canvas coordinate");
    }
}
} // namespace

/// @brief Seed transient selection from the persisted primary item.
workspace::workspace(session& owner) : owner_{owner}
{
    if (owner.current().selection.kind != item_kind::none)
    {
        selected_.push_back(owner.current().selection);
    }
}

/// @brief Borrow history after checking the coordinator's affinity.
session& workspace::edit() const
{
    (void)owner_.current();
    return owner_;
}

/// @brief Toggle or replace transient selection without creating an undo record.
void workspace::select(item_ref item, bool extend)
{
    const auto& source = owner_.current();
    if (gesture_ || !detail::contains(source, item))
    {
        throw std::invalid_argument("Invalid Flow selection during gesture");
    }

    if (!extend)
    {
        selected_.clear();
    }

    if (item.kind != item_kind::none)
    {
        const auto found = std::ranges::find(selected_, item);
        if (found == selected_.end())
        {
            selected_.push_back(std::move(item));
        }
        else if (extend)
        {
            selected_.erase(found);
        }
    }
}

/// @brief Prune identities removed by independent undo, redo or document edits.
std::vector<item_ref> workspace::selection() const
{
    const auto& source = owner_.current();
    auto result = selected_;
    std::erase_if(result,
                  [&](const auto& item)
                  {
                      return !detail::contains(source, item);
                  });
    return result;
}

/// @brief Combine persisted coordinates with the current uncommitted gesture offset.
point workspace::position(const item_ref& item) const
{
    const auto& source = owner_.current();
    const auto found = std::ranges::find(source.layout, item, &item_layout::item);
    point result = found == source.layout.end() ? point{} : point{found->x, found->y};
    if (gesture_ && std::ranges::find(selected_, item) != selected_.end())
    {
        result.x += delta_.x;
        result.y += delta_.y;
    }

    return result;
}

/// @brief Capture exact revision and selected identities before moving any node.
void workspace::begin_move()
{
    (void)owner_.current();
    if (gesture_ || selection().empty())
    {
        throw std::logic_error("Flow move requires an idle nonempty selection");
    }

    selected_ = selection();
    gesture_ = owner_.revision();
    delta_ = {};
}

/// @brief Update only pending displacement, leaving source and history untouched.
void workspace::move_by(point delta)
{
    (void)owner_.current();
    finite(delta);
    if (!gesture_)
    {
        throw std::logic_error("Flow move has not started");
    }

    delta_ = delta;
}

/// @brief Release commits one complete revision; cancellation and stale gestures never publish.
void workspace::end_move(bool cancel)
{
    const auto revision = owner_.revision();
    if (!gesture_)
    {
        return;
    }

    const auto original = std::exchange(gesture_, 0);
    if (cancel)
    {
        delta_ = {};
        return;
    }

    if (original != revision)
    {
        delta_ = {};
        throw std::logic_error("Stale Flow canvas gesture");
    }

    if (delta_ == point{})
    {
        return;
    }

    auto source = owner_.current();
    for (const auto& item : selected_)
    {
        auto found = std::ranges::find(source.layout, item, &item_layout::item);
        if (found == source.layout.end())
        {
            source.layout.push_back({item, item.id, 0, 0});
            found = std::prev(source.layout.end());
        }

        found->x += delta_.x;
        found->y += delta_.y;
    }

    delta_ = {};
    owner_.apply(std::move(source));
}

/// @brief Translate the view without allocating a new authored revision.
void workspace::pan(point delta)
{
    (void)owner_.current();
    finite(delta);
    const point result{pan_.x + delta.x, pan_.y + delta.y};
    finite(result);
    pan_ = result;
}

/// @brief Change scale while retaining the graph coordinate beneath the view anchor.
void workspace::zoom_at(double factor, point anchor)
{
    const auto graph = to_graph(anchor);
    finite(anchor);
    if (!std::isfinite(factor) || factor <= 0)
    {
        throw std::invalid_argument("Invalid Flow canvas zoom");
    }

    const auto scale = std::clamp(zoom_ * factor, 0.25, 4.0);
    const point origin{anchor.x - graph.x * scale, anchor.y - graph.y * scale};
    finite(origin);
    zoom_ = scale;
    pan_ = origin;
}

/// @brief Convert view coordinates to graph coordinates using the current bounded transform.
point workspace::to_graph(point view) const
{
    (void)owner_.current();
    finite(view);
    return {(view.x - pan_.x) / zoom_, (view.y - pan_.y) / zoom_};
}

/// @brief Convert authored coordinates to the logical graph viewport.
point workspace::to_view(point graph) const
{
    (void)owner_.current();
    finite(graph);
    return {graph.x * zoom_ + pan_.x, graph.y * zoom_ + pan_.y};
}

/// @brief Read view scale on the graph's owning coordinator.
double workspace::zoom() const
{
    (void)owner_.current();
    return zoom_;
}
} // namespace atlas::editor_flow
