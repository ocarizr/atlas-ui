/** @file @brief Publish dirty model values only at a caller-selected UI command boundary. */
#include "binding_state.hpp"

namespace atlas::ui_mvvm
{
/// @brief Validate conversions/live identities before any UI command; stale targets retire safely.
void binding_session::publish()
{
    impl_->check();
    const operation_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.mvvm.publish", "ui.cpu"}};
    impl_->prepared.clear();
    for (auto& binding : impl_->state->bindings)
    {
        if (!binding.active)
        {
            continue;
        }

        if (!impl_->tree.contains(binding.target))
        {
            impl_->retire(binding);
            continue;
        }

        if (binding.dirty)
        {
            impl_->prepared.push_back({binding.target, binding.property,
                                       to_ui(*impl_->model, binding.field, binding.property),
                                       impl_->producer, 0});
        }
    }

    for (const auto& write : impl_->prepared)
    {
        impl_->tree.commands().set(write.target, write.field, write.value, write.producer);
    }

    for (auto& binding : impl_->state->bindings)
    {
        binding.dirty = false;
    }
}
} // namespace atlas::ui_mvvm
