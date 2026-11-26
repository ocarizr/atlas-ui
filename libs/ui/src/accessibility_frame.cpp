/** @file @brief Serial accessible publication shares the normal UI owner phase. */
#include <algorithm>
#include <atlas/ui/accessibility_frame.hpp>
#include <iterator>

namespace atlas::ui
{
/// @brief Drain bounded native requests once for this window owner frame.
accessibility_frame::accessibility_frame(accessibility::channel& channel,
                                         accessibility_view coordinates, std::uint64_t focus_owner)
    : channel_{channel}, coordinates_{coordinates}, requests_{channel.take_requests()},
      focus_owner_{focus_owner}
{
}

/// @brief Clear another domain's focus before delivering generation-checked queued requests.
void accessibility_frame::route(input_session& input)
{
    if (previous_focus_.size() == 256)
    {
        throw std::length_error("accessible active-view count exhausted");
    }

    const auto focused = input.focused();
    previous_focus_.push_back(focused);
    if (focused.generation && focus_owner_ && focused.session != focus_owner_)
    {
        input.focus({});
    }

    input.process_accessibility(requests_, coordinates_.modal_root);
    const auto current = input.focused();
    for (std::size_t i = 0; i < requests_.size(); ++i)
    {
        const auto& request = requests_[i];
        if (request.operation == accessibility::action::focus && current.generation &&
            request.target.session == current.session && request.target.index == current.index &&
            request.target.generation == current.generation && i + 1 >= focus_request_)
        {
            focus_request_ = i + 1;
            focus_owner_ = current.session;
        }
    }
}

/// @brief Changes caused by real input select the same exclusive owner as assistive focus.
void accessibility_frame::capture(input_session& input, const accessible_text& resolve)
{
    const auto current = input.focused();
    if (current.generation && !focus_request_ &&
        (!focus_owner_ || std::ranges::find(previous_focus_, current) == previous_focus_.end()))
    {
        focus_owner_ = current.session;
    }

    if (current.generation && focus_owner_ && current.session != focus_owner_)
    {
        input.focus({});
    }

    auto values = input.accessibility_nodes(coordinates_, resolve);
    if (nodes_.size() + values.size() > accessibility::limits{}.max_nodes)
    {
        throw std::length_error("accessible active-view budget exhausted");
    }

    nodes_.insert(nodes_.end(), std::make_move_iterator(values.begin()),
                  std::make_move_iterator(values.end()));
}

/// @brief An unchanged frame keeps its immutable snapshot and avoids redundant native events.
void accessibility_frame::publish()
{
    if (std::ranges::none_of(nodes_,
                             [&](const auto& node)
                             {
                                 return node.id.session == focus_owner_;
                             }))
    {
        focus_owner_ = 0;
    }

    for (auto& node : nodes_)
    {
        node.focused = node.focused && node.id.session == focus_owner_;
    }

    const auto previous = channel_.read();
    if (!previous || !std::ranges::equal(previous->nodes(), nodes_))
    {
        channel_.publish(std::move(nodes_));
    }
}

/// @brief Report the exclusive domain to retain across application frames.
std::uint64_t accessibility_frame::focus_owner() const noexcept
{
    return focus_owner_;
}

/// @brief Only the host can preserve a logical focus bookmark across a reconstructed view.
void accessibility_frame::focus_owner(std::uint64_t session) noexcept
{
    focus_owner_ = session;
}

/// @brief Borrow queued value-only actions for active-domain coordination.
std::span<const accessibility::request> accessibility_frame::requests() const noexcept
{
    return requests_;
}

/// @brief Invalidate all exposed domains when the application hides its window.
void accessibility_frame::clear()
{
    nodes_.clear();
    focus_owner_ = 0;
}

/// @brief Mark contributions that precede a replaceable editor view group.
std::size_t accessibility_frame::checkpoint() const noexcept
{
    return nodes_.size();
}

/// @brief Remove retired view identities without copying preceding contributions.
void accessibility_frame::rollback(std::size_t checkpoint)
{
    if (checkpoint > nodes_.size())
    {
        throw std::invalid_argument("invalid accessible checkpoint");
    }

    nodes_.resize(checkpoint);
}
} // namespace atlas::ui
