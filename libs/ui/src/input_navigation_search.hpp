/** @file @brief Pure deterministic candidate ranking independent of input callback ownership. */
#pragma once
#include <atlas/ui/focus_navigation.hpp>
#include <atlas/ui/layout_types.hpp>
#include <cmath>
#include <tuple>

namespace atlas::ui::detail
{
/// @brief One prevalidated eligible target with a stable captured layout tie-break.
struct focus_candidate
{
    element_handle target{};    ///< Empty marks a missing origin.
    layout_rect bounds{};       ///< Frozen presented hull, not clipped viewport intersection.
    std::int32_t tab_order = 0; ///< Author order override.
    std::size_t ordinal = 0;    ///< Captured layout tie-break.
    bool tab_stop = true;       ///< Ordered traversal participation.
};

/// @brief Flip left/up so a forward primary coordinate always increases.
struct focus_projection
{
    double start; ///< Primary leading edge.
    double end;   ///< Primary trailing edge.
    double low;   ///< Perpendicular leading edge.
    double high;  ///< Perpendicular trailing edge.
};

/// @brief Projection uses double arithmetic to avoid overflowing finite float hulls.
inline focus_projection project_focus(layout_rect bounds, focus_direction direction)
{
    const bool horizontal =
        direction == focus_direction::left || direction == focus_direction::right;
    const bool reverse = direction == focus_direction::left || direction == focus_direction::up;
    const double start = horizontal ? bounds.x : bounds.y;
    const double end = start + (horizontal ? bounds.width : bounds.height);
    const double low = horizontal ? bounds.y : bounds.x;
    return {reverse ? -end : start, reverse ? -start : end, low,
            low + (horizontal ? bounds.height : bounds.width)};
}

using focus_score =
    std::tuple<int, double, double, double, std::size_t>; ///< Beam/gap/stable spatial rank.
using focus_order =
    std::tuple<std::int32_t, std::size_t>; ///< Author order then captured layout order.

/// @brief Beam overlap precedes edge distance and deterministic cross/forward center ties.
inline focus_score rank_focus(focus_projection origin, focus_projection target, bool wrap,
                              std::size_t ordinal)
{
    const bool beam = origin.low < target.high && target.low < origin.high;
    const double gap = std::max(0., target.start - origin.end);
    const double cross = std::max({0., origin.low - target.high, target.low - origin.high});
    return {beam ? 0 : 1, wrap ? (target.start + target.end) / 2 : gap * gap + cross * cross,
            std::abs(target.low + target.high - origin.low - origin.high),
            target.start + target.end - origin.start - origin.end, ordinal};
}

/// @brief Fixed stack accumulator; offering a candidate never allocates or dispatches a callback.
class focus_search
{
  public:
    /// @brief Initialize a value-only accumulator without retaining an input session.
    focus_search(focus_direction direction, focus_candidate origin)
        : direction_(direction), origin_(origin)
    {
    }

    ///< Borrow no external state; origin is a small immutable value.
    /// @brief Reduce one eligible candidate in stable traversal order.
    void offer(focus_candidate candidate)
    {
        if (candidate.target == origin_.target)
        {
            return;
        }

        if (direction_ == focus_direction::next || direction_ == focus_direction::previous)
        {
            offer_ordered(candidate);
        }
        else if (!origin_.target.generation)
        {
            if (!selected_.target.generation || candidate.ordinal < selected_.ordinal)
            {
                selected_ = candidate;
            }
        }
        else
        {
            offer_spatial(candidate);
        }
    }

    /// @brief Return a forward candidate, or an opposite endpoint when wrapping is permitted.
    element_handle selected(bool wrap) const
    {
        return selected_.target.generation ? selected_.target
               : wrap                      ? wrapped_.target
                                           : element_handle{};
    }

    ///< Wrap is consulted only after forward selection fails.
  private:
    /// @brief Ordered endpoints preserve author order and do not depend on painter Z order.
    void offer_ordered(focus_candidate candidate)
    {
        if (!candidate.tab_stop)
        {
            return;
        }

        const bool previous = direction_ == focus_direction::previous;
        const auto key = [](focus_candidate value)
        {
            return focus_order{value.tab_order, value.ordinal};
        };
        const auto value = key(candidate);
        const bool forward =
            !origin_.target.generation || (previous ? value < key(origin_) : value > key(origin_));
        if (forward && (!selected_.target.generation ||
                        (previous ? value > key(selected_) : value < key(selected_))))
        {
            selected_ = candidate;
        }

        if (!wrapped_.target.generation ||
            (previous ? value > key(wrapped_) : value < key(wrapped_)))
        {
            wrapped_ = candidate;
        }
    }

    /// @brief Opposite-edge wrapping uses the same perpendicular beam preference.
    void offer_spatial(focus_candidate candidate)
    {
        const auto origin = project_focus(origin_.bounds, direction_);
        const auto bounds = project_focus(candidate.bounds, direction_);
        const auto score = rank_focus(origin, bounds, false, candidate.ordinal);
        if (bounds.start + bounds.end > origin.start + origin.end &&
            (!selected_.target.generation || score < selected_rank_))
        {
            selected_ = candidate;
            selected_rank_ = score;
        }

        const auto opposite = rank_focus(origin, bounds, true, candidate.ordinal);
        if (!wrapped_.target.generation || opposite < wrap_rank_)
        {
            wrapped_ = candidate;
            wrap_rank_ = opposite;
        }
    }

    focus_direction direction_;   ///< Request fixed for this scan.
    focus_candidate origin_;      ///< Validated origin and order.
    focus_candidate selected_;    ///< Best forward candidate.
    focus_candidate wrapped_;     ///< Best opposite-edge candidate.
    focus_score selected_rank_{}; ///< Forward score when selected exists.
    focus_score wrap_rank_{};     ///< Opposite score when wrapped exists.
};
} // namespace atlas::ui::detail
