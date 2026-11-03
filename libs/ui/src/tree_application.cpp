/** @file @brief Validate complete packets before nonallocating requested-state publication. */
#include "tree_state.hpp"

namespace atlas::ui
{
namespace
{
/// @brief Decode only validated width/height commands; zero and automatic are distinct.
std::pair<float, bool> extent_value(const property_value& value)
{
    return std::holds_alternative<std::monostate>(value) ? std::pair{0.f, false}
                                                         : std::pair{std::get<float>(value), true};
}
} // namespace

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

    bool geometry_changed = false;
    for (const auto& write : batch.writes())
    {
        const auto& current = impl_->at(write.target.index).requested;
        if (write.field == property::width || write.field == property::height)
        {
            const auto [value, fixed] = extent_value(write.value);
            geometry_changed = geometry_changed ||
                               (write.field == property::width
                                    ? current.width != value || current.width_fixed != fixed
                                    : current.height != value || current.height_fixed != fixed);
        }
    }

    if (geometry_changed)
    {
        impl_->ensure_layout_revision();
    }

    for (const auto& write : batch.writes())
    {
        auto& value = impl_->at(write.target.index).requested;
        bool changed = false;
        bool layout = false;
        switch (write.field)
        {
            case property::width:
            {
                const auto [extent, fixed] = extent_value(write.value);
                changed = value.width != extent || value.width_fixed != fixed;
                value.width = extent;
                value.width_fixed = fixed;
                layout = true;
                break;
            }
            case property::height:
            {
                const auto [extent, fixed] = extent_value(write.value);
                changed = value.height != extent || value.height_fixed != fixed;
                value.height = extent;
                value.height_fixed = fixed;
                layout = true;
                break;
            }
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
    if (geometry_changed)
    {
        ++impl_->layout_revision;
    }
}
} // namespace atlas::ui
