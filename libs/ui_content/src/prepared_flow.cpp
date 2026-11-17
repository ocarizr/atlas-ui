/** @file @brief Prepare and verify Flow graph dependencies using the typed catalog. */
#include "prepared_binary.hpp"
#include "prepared_flow_loader.hpp"
#include "storage.hpp"
#include <map>
#include <set>

namespace atlas::ui_content
{
namespace
{
using closure_map = std::map<assets::asset_id, prepared_dependency>; ///< Unique typed identities.
using screen_set = std::set<assets::asset_ref>; ///< Distinct assets referenced by menu nodes.

/// @brief Catalog dependencies describe authored identities independent of paths or encoding.
void add_dependency(closure_map& closure, const prepared_dependency& entry)
{
    (void)assets::format_id(entry.source.id);
    const auto [found, inserted] = closure.try_emplace(entry.source.id, entry);
    if (!entry.revision || (!inserted && found->second != entry))
    {
        throw std::invalid_argument("Conflicting prepared Flow dependency revisions");
    }

    if (closure.size() > detail::max_prepared_dependencies)
    {
        throw std::length_error("Prepared Flow dependency closure exceeds its budget");
    }
}

/// @brief View dependencies are restricted to the asset families admitted by the view codec.
bool view_dependency(assets::asset_ref screen, assets::asset_ref reference)
{
    switch (reference.type)
    {
        case assets::asset_type::ui:
            return reference == screen;
        case assets::asset_type::view_model:
        case assets::asset_type::script:
        case assets::asset_type::texture:
        case assets::asset_type::shader:
        case assets::asset_type::material:
            return true;
        default:
            return false;
    }
}

/// @brief Require a unique sorted closure including each view's own revision.
void add_screen(closure_map& closure, const prepared_flow_screen& screen)
{
    if (screen.dependencies.size() > detail::max_prepared_dependencies)
    {
        throw std::length_error("Prepared Flow screen dependency budget exceeded");
    }

    std::optional<assets::asset_ref> previous;
    bool includes_screen = false;
    for (const auto& entry : screen.dependencies)
    {
        if ((previous && *previous >= entry.source) ||
            !view_dependency(screen.source, entry.source))
        {
            throw std::invalid_argument("Invalid prepared Flow screen dependency table");
        }

        add_dependency(closure, entry);
        includes_screen |= entry.source == screen.source;
        previous = entry.source;
    }

    if (!includes_screen)
    {
        throw std::invalid_argument("Prepared Flow screen is missing its own revision");
    }
}

/// @brief Collect unique typed references even when several menu IDs use one UI document.
screen_set screen_assets(const flow_document& graph)
{
    screen_set result;
    for (const auto& screen : graph.screens)
    {
        result.insert(screen.asset);
    }

    return result;
}

/// @brief Reject missing authored element bindings while preparation has the UI document.
void validate_screen_controls(const flow_document& graph, const prepared_view& view)
{
    std::set<std::string_view> ids;
    std::set<std::string_view> menus;
    for (const auto& element : view.view.elements)
    {
        ids.insert(element.id);
    }

    for (const auto& screen : graph.screens)
    {
        if (screen.asset == view.source)
        {
            menus.insert(screen.id);
            if (!screen.focus.empty() && !ids.contains(screen.focus))
            {
                throw std::invalid_argument("Prepared Flow focus element is missing: " +
                                            screen.focus);
            }
        }
    }

    for (const auto& edge : graph.transitions)
    {
        if (edge.enabled && menus.contains(edge.from) && !edge.control.empty() &&
            !ids.contains(edge.control))
        {
            throw std::invalid_argument("Prepared Flow control element is missing: " +
                                        edge.control);
        }
    }
}
} // namespace

/// @brief Validate exact direct screen mappings and merge their dependency closure.
std::vector<prepared_dependency> flow_dependencies(const prepared_flow& source)
{
    validate_flow(source.graph);
    if (source.source.source.type != assets::asset_type::flow)
    {
        throw std::invalid_argument("Prepared Flow record requires a Flow asset identity");
    }

    const auto expected = screen_assets(source.graph);
    if (!std::ranges::equal(source.screens, expected, {}, &prepared_flow_screen::source))
    {
        throw std::invalid_argument("Prepared Flow screen table does not match its graph");
    }

    closure_map closure;
    add_dependency(closure, source.source);
    for (const auto& screen : source.screens)
    {
        add_screen(closure, screen);
    }

    std::vector<prepared_dependency> result;
    result.reserve(closure.size());
    for (const auto& entry : closure)
    {
        result.push_back(entry.second);
    }

    return result;
}

/// @brief Resolve every screen once using the same validation as the normal UI cooker.
prepared_flow prepare_flow(assets::asset_ref source, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.flow.prepare", "ui.content"}};
    auto graph = load_flow(source, options);
    prepared_flow result{{source, options.storage.describe(source).revision}, std::move(graph), {}};
    for (const auto& screen : screen_assets(result.graph))
    {
        auto view = prepare_view(screen, options);
        validate_screen_controls(result.graph, view);
        result.screens.push_back({screen, std::move(view.dependencies)});
    }

    detail::validate_prepared_mount(flow_dependencies(result), options);
    return result;
}

/// @brief Load the complete graph record and check revisions against the current catalog.
prepared_flow load_prepared_flow(assets::asset_ref source, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.flow.load_prepared", "ui.content"}};
    if (source.type != assets::asset_type::flow)
    {
        throw std::invalid_argument("Prepared Flow load requires a Flow asset");
    }

    const auto bytes =
        detail::acquire(options.storage.describe(source), detail::max_prepared_bytes, options);
    auto result = decode_prepared_flow(std::as_bytes(std::span{bytes}));
    if (result.source.source != source)
    {
        throw std::invalid_argument("Prepared Flow record identity differs from its catalog entry");
    }

    detail::validate_prepared_mount(flow_dependencies(result), options);
    return result;
}

/// @brief Keep staged screen reads and owner publication in the same Flow Manager implementation.
std::unique_ptr<flow_manager> build_prepared_flow(const prepared_flow& source,
                                                  const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.flow.build_prepared", "ui.content"}};
    detail::validate_prepared_mount(flow_dependencies(source), options);
    if (!options.materials.empty() || !options.model_schemas.empty())
    {
        throw std::invalid_argument("Prepared screens already own their model and material tables");
    }

    return std::make_unique<flow_manager>(
        source.graph, detail::make_prepared_flow_loader(options, source.screens),
        options.diagnostics);
}
} // namespace atlas::ui_content
