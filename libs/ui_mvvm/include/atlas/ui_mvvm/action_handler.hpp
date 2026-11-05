/**
 * @file
 * @brief Data-authored routing from UI events to independent ViewModel action protocols. */
#pragma once
#include <atlas/mvvm/view_model.hpp>
#include <atlas/ui/input_session.hpp>

namespace atlas::ui_mvvm
{
/// @brief Serializable binding data; UI document persistence remains the UI content boundary.
struct action_binding_definition
{
    ui::event_kind event = ui::event_kind::pointer_up; ///< Authored UI trigger.
    std::string action;                   ///< Named ViewModel action resolved once at setup.
    std::vector<mvvm::value> arguments{}; ///< Owned typed constant payload.
    ui::route_phase route = ui::route_phase::target; ///< Explicit propagation stage.
    std::uint32_t input_action = 0; ///< Required semantic input ID for event_kind::action.
    input::action_phase input_phase =
        input::action_phase::performed; ///< Semantic input phase filter.
};

/// @brief Generic input handler; fields/action mappings require no per-ViewModel native subclass.
class action_handler final : public ui::input_handler
{
  public:
    action_handler(
        std::shared_ptr<mvvm::view_model> model,
        std::span<const action_binding_definition> bindings,
        diagnostics::services destinations = {}); ///< Copy/validate at most 64 authored routes.
    ~action_handler() override; ///< Owner-only cleanup; no model callbacks on destruction.
    /// @brief Enqueue matching action demands at the serial UI callback boundary.
    ui::event_response on_event(const ui::routed_event& event,
                                ui::command_buffer& commands) override;
    ///< Enqueue matching model actions; frame synchronization/flush remains caller-owned.
  private:
    class impl;                  ///< Immutable compiled action routes and owner dispatch guard.
    std::unique_ptr<impl> impl_; ///< Retains model and protocol arguments, never a UI tree.
};
} // namespace atlas::ui_mvvm
