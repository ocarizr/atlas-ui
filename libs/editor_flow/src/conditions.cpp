/** @file @brief Persistent outgoing AND/NOT predicates resolve to owner registry generations. */
#include "document_state.hpp"
#include <algorithm>
#include <atlas/editor_flow/inspector.hpp>
#include <cmath>
#include <set>

namespace atlas::editor_flow
{
namespace
{
/// @brief Bound recursive authoring before serialization or copying into runtime bindings.
void shape(const authored_condition& node, std::size_t depth, std::size_t& count)
{
    if (++count > 256 || depth > 8 || node.arguments.size() > 32 || node.children.size() > 64)
    {
        throw std::length_error("Flow condition expression budget exhausted");
    }

    mvvm::validate_text(node.callable, 128);
    mvvm::validate_text(node.context, 64);
    if ((!node.callable.empty() && !node.children.empty()) ||
        (node.callable.empty() && (!node.arguments.empty() || !node.context.empty())))
    {
        throw std::invalid_argument("Flow condition must be a leaf or an AND group");
    }

    for (const auto& value : node.arguments)
    {
        if (const auto* text = std::get_if<std::string>(&value))
        {
            mvvm::validate_text(*text, 4096);
        }

        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        {
            throw std::invalid_argument("Nonfinite condition argument");
        }
    }

    for (const auto& child : node.children)
    {
        shape(child, depth + 1, count);
    }
}

/// @brief Unknown or incompatible predicates fail before negation can grant permission.
scripting::condition_node resolve(const authored_condition& source, scripting::domain& scripts,
                                  const std::vector<scripting::callable_descriptor>& registry)
{
    scripting::condition_node result;
    result.negate = source.negate;
    result.arguments = source.arguments;
    result.children.reserve(source.children.size());
    if (!source.callable.empty())
    {
        const auto found =
            std::ranges::find(registry, source.callable, &scripting::callable_descriptor::id);
        if (found == registry.end())
        {
            throw std::invalid_argument("Unresolved Flow predicate: " + source.callable);
        }

        validate_arguments(*found, source.arguments);
        result.binding = scripts.bind_callable(source.callable, source.context);
    }

    for (const auto& child : source.children)
    {
        result.children.push_back(resolve(child, scripts, registry));
    }

    return result;
}
} // namespace

/// @brief Missing registrations remain editable drafts, but route ownership and budgets are exact.
void validate_conditions(const document& source)
{
    if (source.conditions.size() > 1024)
    {
        throw std::length_error("Excess outgoing condition groups");
    }

    std::set<std::string_view> routes;
    for (const auto& entry : source.conditions)
    {
        if (!detail::contains(source, {item_kind::transition, entry.transition}) ||
            !routes.insert(entry.transition).second)
        {
            throw std::invalid_argument("Unknown or duplicate condition route");
        }

        std::size_t count = 0;
        shape(entry.expression, 0, count);
    }
}

/// @brief An explicit apply operation resolves schemas once rather than scanning Lua per frame.
std::vector<ui_content::flow_condition_binding> resolve_conditions(const document& source,
                                                                   scripting::domain& scripts)
{
    validate_conditions(source);
    const auto registry = callable_choices(scripts, scripting::callable_kind::condition);
    std::vector<ui_content::flow_condition_binding> result;
    result.reserve(source.conditions.size());
    for (const auto& entry : source.conditions)
    {
        result.push_back({entry.transition, resolve(entry.expression, scripts, registry)});
    }

    return result;
}
} // namespace atlas::editor_flow
