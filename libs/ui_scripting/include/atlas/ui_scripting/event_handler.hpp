/**
 * @file
 * @brief Shared UI lifecycle/input adapter for independent owner-thread Lua sessions.
 */
#pragma once
#include <atlas/scripting/session.hpp>
#include <atlas/ui/input_session.hpp>
#include <thread>

namespace atlas::ui_scripting
{
/// @brief Forward UI events to named handlers; model demands remain deferred until sync points.
class event_handler final : public ui::input_handler
{
  public:
    explicit event_handler(std::shared_ptr<scripting::session> behavior,
                           diagnostics::services destinations = {}); ///< Retain behavior owner.
    ~event_handler() override; ///< Release on the VM owner outside event dispatch.
    /// @brief Forward the event vocabulary and scalar metadata on the owning thread.
    ui::event_response on_event(const ui::routed_event& event,
                                ui::command_buffer& commands) override;
    ///< Dispatch the lifecycle name with scalar input metadata; no direct tree mutation.
  private:
    std::shared_ptr<scripting::session> behavior_; ///< Shared behavior and model lifetime.
    diagnostics::services destinations_;           ///< Optional event CPU capture.
    const std::thread::id owner_ = std::this_thread::get_id(); ///< Native UI owner identity.
};
} // namespace atlas::ui_scripting
