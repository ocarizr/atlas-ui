/** @file @brief Publish complete menus and retire outgoing UI lifecycle on the owner. */
#include "flow_state.hpp"
#include <algorithm>

namespace atlas::ui_content
{
/// @brief A sync point must precede the next UI frame on every presented composition.
void flow_manager::impl::check_frame(std::uint64_t next) const
{
    if (next < 2 || next <= frame)
    {
        throw std::invalid_argument("Flow requires an increasing owner frame");
    }

    for (const auto& stack : current)
    {
        for (const auto& entry : stack)
        {
            if (entry.session)
            {
                const auto input = entry.session->view->input().statistics();
                if ((!input.closed && !input.failed) || input.frame >= next)
                {
                    throw std::logic_error("Flow sync requires closed input frames");
                }
            }
        }
    }
}

/// @brief Retain ready views privately while other active regions are still loading.
bool flow_manager::impl::poll()
{
    if (!candidate)
    {
        throw std::logic_error("Missing flow candidate");
    }

    bool complete = true;
    for (auto& pending : candidate->pending)
    {
        if (!pending.load)
        {
            continue;
        }

        auto ready = pending.load->poll();
        if (!ready)
        {
            complete = false;
            continue;
        }

        auto& entry = candidate->stacks.at(pending.position.region).at(pending.position.index);
        const auto& authored = graph.screens.at(entry.screen).focus;
        if (!authored.empty())
        {
            (void)ready->find(authored);
        }

        validate_view(pending, *ready);
        entry.session = std::make_shared<detail::flow_session>(
            detail::flow_session{pending.generation, std::move(ready)});
        entry.focus_pending = true;
        pending.load.reset();
    }

    return complete;
}

namespace
{
/// @brief Capture the outgoing input focus by authored ID, including overlay suspension.
void remember(const detail::flow_stacks& source, detail::flow_stacks& target,
              const std::set<std::uint64_t>& old_inputs, const std::set<std::uint64_t>& new_inputs)
{
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        for (const auto& entry : source.at(index))
        {
            if (!entry.session || !old_inputs.contains(entry.session->generation) ||
                new_inputs.contains(entry.session->generation))
            {
                continue;
            }

            auto& stack = target.at(index);
            const auto retained =
                std::ranges::find(stack, entry.screen, &detail::flow_entry::screen);
            if (retained != stack.end())
            {
                auto& view = *entry.session->view;
                retained->focus = view.authored_id(view.input().focused());
                retained->focus_pending = true;
            }
        }
    }
}

/// @brief Compare shared view identities without allocating during retirement.
bool retains(const detail::flow_stacks& state, const std::shared_ptr<detail::flow_session>& view)
{
    return std::ranges::any_of(state,
                               [&](const auto& stack)
                               {
                                   return std::ranges::any_of(stack,
                                                              [&](const auto& entry)
                                                              {
                                                                  return entry.session == view;
                                                              });
                               });
}
} // namespace

/// @brief Prepare foreground tokens and bookmarks before replacing the live history.
void flow_manager::impl::publish(std::uint64_t next)
{
    if (!candidate)
    {
        throw std::logic_error("Missing flow candidate");
    }

    auto& state = candidate->stacks;
    std::set<std::uint64_t> inputs;
    for (const auto& position : visible(state))
    {
        const auto& entry = state.at(position.region).at(position.index);
        if (position.input && entry.session)
        {
            inputs.insert(entry.session->generation);
        }
    }

    remember(current, state, admission->inputs, inputs);
    const auto old_order = visible(current);
    current.swap(state);
    admission->inputs.swap(inputs);
    for (auto position = old_order.rbegin(); position != old_order.rend(); ++position)
    {
        auto& entry = state.at(position->region).at(position->index);
        if (!entry.session)
        {
            continue;
        }

        const auto generation = entry.session->generation;
        const bool kept = retains(current, entry.session);
        const bool masked = inputs.contains(generation) && !admission->inputs.contains(generation);
        if (kept && !masked)
        {
            continue;
        }

        try
        {
            auto& view = *entry.session->view;
            view.begin_models(next);
            if (kept)
            {
                view.input().suspend(next);
            }
            else
            {
                view.input().shutdown(next);
            }

            view.synchronize();
            view.tree().apply(view.tree().commands().seal());
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }

        entry.session.reset();
    }
}

/// @brief Dropping a pending transaction cancels its uncommitted requests.
void flow_manager::impl::clear_candidate() noexcept
{
    candidate.reset();
    admission->command.reset();
    admission->busy = false;
}
} // namespace atlas::ui_content
