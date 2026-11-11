/** @file @brief Prepare a complete tree, model graph and shared-domain candidate. */
#include "document_validation.hpp"
#include "instance_state.hpp"
#include "routes.hpp"

namespace atlas::ui_content
{
namespace
{
/// @brief Size initial capacities to the bounded authored hierarchy and binding counts.
ui::tree_limits tree_limits(const document& source)
{
    ui::tree_limits limits;
    limits.max_elements = static_cast<std::uint32_t>(source.elements.size());
    limits.commands.max_commands =
        std::max(limits.commands.max_commands, source.elements.size() * 4);
    return limits;
}

/// @brief Count actual subscriptions per named context before creating any adapter.
auto binding_counts(const document& source)
{
    std::map<std::string_view, std::size_t> counts;
    for (const auto& node : source.elements)
    {
        for (const auto& property : node.properties)
        {
            ++counts[property.model];
        }
    }

    return counts;
}
} // namespace

/// @brief Install subscriptions and handlers only inside the private candidate.
instance::impl::impl(const document& source, const construction& options)
    : tree{tree_limits(source), options.memory, options.diagnostics},
      elements{detail::build_tree(tree, source)}, models{detail::make_models(source, options)},
      domain{std::make_shared<scripting::domain>(scripting::session_limits{}, options.memory,
                                                 options.diagnostics, options.script_diagnostics)},
      input{tree,
            {.max_nodes = source.elements.size(), .max_bindings = source.elements.size()},
            options.memory,
            options.diagnostics},
      diagnostics{options.diagnostics}
{
    const auto counts = binding_counts(source);
    std::uint64_t producer = 1;
    for (const auto& context : source.models)
    {
        model_order.push_back(context.name);
        const auto& model = models.at(context.name);
        domain->bind_context(context.name, model);
        if (const auto count = counts.find(context.name); count != counts.end())
        {
            bindings.emplace(context.name,
                             std::make_unique<ui_mvvm::binding_session>(
                                 tree, model, ++producer, ui_mvvm::binding_limits{count->second},
                                 options.memory, diagnostics));
        }
    }

    bind_elements(source, options);
}

/// @brief Resolve binding targets once, retaining their runtime reverse-write handles privately.
void instance::impl::bind_properties(const element& node, ui::element_handle target)
{
    for (const auto& property : node.properties)
    {
        auto& binding = *bindings.at(property.model);
        const auto token = binding.bind(target, property.binding);
        properties.emplace(std::pair{target.index, property.binding.target},
                           bound_property{&binding, token});
    }
}

/// @brief Resolve texture references, input policies, model routes, and isolated Lua behaviors.
void instance::impl::bind_elements(const document& source, const construction& options)
{
    std::set<assets::asset_id> registered;
    for (const auto& node : source.elements)
    {
        try
        {
            const auto target = elements.at(node.id);
            bind_properties(node, target);
            if (node.texture)
            {
                (void)options.storage.describe(*node.texture);
                textures.emplace(node.id, *node.texture);
            }

            if (node.interaction || !node.actions.empty() || node.script)
            {
                input.bind(target, detail::make_handler(node, models, domain, registered, options),
                           node.interaction.value_or(ui::interaction_options{}));
            }
        }
        catch (const std::logic_error& error)
        {
            throw std::invalid_argument("UI element " + node.id + ": " + error.what());
        }
    }
}

/// @brief Validate all inputs before the candidate can become the active UI.
std::unique_ptr<instance> instance::build(const document& source, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.content.build", "ui.content"}};
    detail::validate(source);
    if (options.reads.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    auto state = std::make_unique<impl>(source, options);
    if (options.reads.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    return std::unique_ptr<instance>{new instance{std::move(state)}};
}
} // namespace atlas::ui_content
