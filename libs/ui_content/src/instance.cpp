/** @file @brief Authored UI owner access and explicit model/behavior frame coordination. */
#include "instance_state.hpp"

namespace atlas::ui_content
{
/// @brief A completed candidate is the sole owner of its composed UI.
instance::instance(std::unique_ptr<impl> state) : impl_{std::move(state)}
{
}

/// @brief Member order releases handlers before scripts, subscriptions, and tree.
instance::~instance()
{
    for (const auto& [model, route] : impl_->animation_routes)
    {
        model->unsubscribe(route);
    }
}

/// @brief An authored ID never resolves to a different element after the original is removed.
ui::element_handle instance::find(std::string_view id) const
{
    const auto found = impl_->elements.find(id);
    if (found == impl_->elements.end() || !impl_->tree.contains(found->second))
    {
        throw std::out_of_range("Unknown or removed UI element: " + std::string{id});
    }

    return found->second;
}

/// @brief Supply a texture reference to the host's typed content and residency pipeline.
std::optional<assets::asset_ref> instance::texture(std::string_view id) const
{
    (void)find(id);
    const auto found = impl_->textures.find(id);
    return found == impl_->textures.end() ? std::nullopt : std::optional{found->second};
}

/// @brief Borrow immutable HLSL and keys; native compilation belongs to the renderer owner.
std::span<const material_shader_source> instance::material_shaders() const
{
    return impl_->material_shaders;
}

/// @brief Reject a composition whose material dependency belongs to a superseded mount.
bool instance::materials_current(const streaming::catalog_streamer& storage) const noexcept
{
    return std::ranges::all_of(impl_->material_dependencies,
                               [&](const auto& dependency)
                               {
                                   return storage.accepts(dependency.second);
                               });
}

/// @brief Resolve a named model without exposing its runtime schema slots in authoring data.
mvvm::view_model& instance::model(std::string_view context)
{
    const auto found = impl_->models.find(context);
    if (found == impl_->models.end())
    {
        throw std::out_of_range("Unknown UI model: " + std::string{context});
    }

    return *found->second;
}

/// @brief The host supplies viewport and layout execution to this private tree.
ui::element_tree& instance::tree()
{
    return impl_->tree;
}

/// @brief Input events enter on the owner through the existing input session.
ui::input_session& instance::input()
{
    return impl_->input;
}

/// @brief Native work and result delivery use the existing Lua domain owner interface.
scripting::domain& instance::scripts()
{
    return *impl_->domain;
}

/// @brief Models and the director enter the same frame before any scripted UI event.
void instance::begin_models(std::uint64_t frame)
{
    if (frame <= 1)
    {
        throw std::invalid_argument("UI authored runtime frames start at 2");
    }

    for (const auto& context : impl_->model_order)
    {
        impl_->models.at(context)->begin_frame(frame);
    }

    impl_->domain->begin_frame(frame);
}

/// @brief The coordinator controls when observer changes and property demands are published.
void instance::synchronize()
{
    const diagnostics::operation_scope scope{impl_->diagnostics, {"ui.content.sync", "ui.content"}};
    for (const auto& context : impl_->model_order)
    {
        impl_->models.at(context)->flush();
    }

    impl_->domain->synchronize();
    for (const auto& context : impl_->model_order)
    {
        if (const auto binding = impl_->bindings.find(context); binding != impl_->bindings.end())
        {
            binding->second->publish();
        }
    }
}

/// @brief Only an explicit reverse demand can update a two-way binding.
void instance::write_back(std::string_view id, ui::property property, ui::property_value value)
{
    const auto found = impl_->properties.find({find(id).index, property});
    if (found == impl_->properties.end())
    {
        throw std::invalid_argument("No authored UI binding for requested property");
    }

    found->second.session->write_back(found->second.token, value);
}
} // namespace atlas::ui_content
