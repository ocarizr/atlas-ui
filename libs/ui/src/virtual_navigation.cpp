/** @file @brief Logical focus and queued navigation independent of recycled widget generations. */
#include "virtual_list_state.hpp"
#include <atlas/input/key.hpp>

namespace atlas::ui::detail
{
namespace
{
/// @brief Resolve keys/actions without ever navigating to an overscan-only physical neighbor.
int movement(const routed_event& event, const scroll_navigation_actions& actions)
{
    if (event.kind == event_kind::key_down)
    {
        if (event.key == static_cast<unsigned>(input::key::up))
        {
            return -1;
        }

        if (event.key == static_cast<unsigned>(input::key::down))
        {
            return 1;
        }

        if (event.key == static_cast<unsigned>(input::key::home))
        {
            return -2;
        }

        if (event.key == static_cast<unsigned>(input::key::end))
        {
            return 2;
        }
    }

    if (event.kind == event_kind::action && event.action &&
        event.action->phase == input::action_phase::performed)
    {
        const auto id = event.action->action;
        if (id && id == actions.previous)
        {
            return -1;
        }

        if (id && id == actions.next)
        {
            return 1;
        }

        if (id && id == actions.first)
        {
            return -2;
        }

        if (id && id == actions.last)
        {
            return 2;
        }
    }

    return 0;
}

/// @brief Queue a logical target; subsequent activation waits for the next realization boundary.
class navigation_proxy final : public input_handler
{
  public:
    /// @brief Retain published logical navigation independently of physical rows.
    explicit navigation_proxy(std::shared_ptr<virtual_navigation> state) : state_(std::move(state))
    {
    }

    event_response on_event(const routed_event& event, command_buffer&) override
    {
        if (event.kind == event_kind::focus_lost && event.target_alive && !state_->restoring)
        {
            state_->focused = 0;
            state_->pending.reset();
        }

        if (!state_->active || !state_->source || !state_->focused)
        {
            return {};
        }

        const auto move = movement(event, state_->actions);
        if (move && state_->source->count())
        {
            const auto current = state_->source->find(state_->pending.value_or(state_->focused));
            auto index = current.value_or(0);
            if (move == -2)
            {
                index = 0;
            }
            else if (move == 2)
            {
                index = state_->source->count() - 1;
            }
            else if (move < 0)
            {
                index -= index ? 1 : 0;
            }
            else
            {
                index = std::min(index + 1, state_->source->count() - 1);
            }

            state_->pending = state_->source->item(index).key;
            return {true};
        }

        const bool activation = event.kind == event_kind::key_down ||
                                event.kind == event_kind::key_up ||
                                event.kind == event_kind::action;
        return {state_->pending.has_value() && activation};
    } ///< No structural UI mutation from inside serial dispatch.
  private:
    std::shared_ptr<virtual_navigation> state_; ///< Owner-thread logical source and pending focus.
};

/// @brief Resources live through cleanup; focus acquisition records the stable key, not a slot.
class binding_proxy final : public input_handler
{
  public:
    /// @brief Retain row resources through ordered user-handler cleanup.
    binding_proxy(std::shared_ptr<input_handler> inner, std::shared_ptr<virtual_navigation> state,
                  item_key key, std::shared_ptr<row_lease> lease)
        : inner_(std::move(inner)), state_(std::move(state)), key_(key), lease_(std::move(lease))
    {
    }

    event_response on_event(const routed_event& event, command_buffer& commands) override
    {
        if (!state_->active)
        {
            return {};
        }

        if (event.kind == event_kind::focus_gained)
        {
            state_->focused = key_;
            state_->pending.reset();
        }

        if (event.kind == event_kind::focus_lost && event.target_alive && !state_->restoring)
        {
            state_->focused = 0;
            state_->pending.reset();
        }

        return inner_ ? inner_->on_event(event, commands) : event_response{};
    } ///< Forward lifecycle cleanup even when the physical generation has already retired.
  private:
    std::shared_ptr<input_handler> inner_;      ///< Optional user behavior.
    std::shared_ptr<virtual_navigation> state_; ///< Logical focus outlives a binding generation.
    item_key key_;                              ///< Captured immutable item identity.
    std::shared_ptr<row_lease>
        lease_; ///< Own subscription/resource lifetime through input cleanup.
};
} // namespace

/// @brief Wrap behavior with logical focus tracking and retained cleanup ownership.
std::shared_ptr<input_handler> row_handler(std::shared_ptr<input_handler> inner,
                                           std::shared_ptr<virtual_navigation> navigation,
                                           item_key key, std::shared_ptr<row_lease> lease)
{
    return std::make_shared<binding_proxy>(std::move(inner), std::move(navigation), key,
                                           std::move(lease));
}

/// @brief Create a serial logical navigation adapter for the owned scroll viewport.
std::shared_ptr<input_handler> navigation_handler(std::shared_ptr<virtual_navigation> navigation)
{
    return std::make_shared<navigation_proxy>(std::move(navigation));
}
} // namespace atlas::ui::detail
