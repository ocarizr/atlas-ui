/** @file @brief Validate complete packets before nonallocating requested-state publication. */
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Immutable packets originate from the sole bound producer; validate freshness atomically.
void element_tree::apply(const command_batch& batch)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.apply", "ui.cpu"}};
    if (batch.session() != impl_->session || batch.frame() < impl_->frame || batch.frame() == 0 ||
        (batch.frame() == impl_->frame ? batch.ordinal() != impl_->ordinal + 1
                                       : batch.ordinal() != 1))
    {
        throw std::invalid_argument("UI tree needs a session-bound batch in frame/ordinal order");
    }

    for (const auto& write : batch.writes())
    {
        (void)impl_->require(write.target);
    }

    for (const auto& write : batch.writes())
    {
        auto& value = impl_->at(write.target.index).requested;
        bool changed = false;
        bool layout = false;
        switch (write.field)
        {
            case property::width:
                changed = value.width != std::get<float>(write.value);
                value.width = std::get<float>(write.value);
                layout = true;
                break;
            case property::height:
                changed = value.height != std::get<float>(write.value);
                value.height = std::get<float>(write.value);
                layout = true;
                break;
            case property::opacity:
                changed = value.opacity != std::get<float>(write.value);
                value.opacity = std::get<float>(write.value);
                break;
            case property::visible:
                changed = value.visible != std::get<bool>(write.value);
                value.visible = std::get<bool>(write.value);
                break;
        }

        if (changed)
        {
            impl_->subtree(write.target.index, layout);
            if (layout)
            {
                impl_->ancestors(impl_->at(write.target.index).parent);
            }
        }
    }

    impl_->frame = batch.frame();
    impl_->ordinal = batch.ordinal();
}
} // namespace atlas::ui
