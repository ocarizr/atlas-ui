/** @file @brief Fail-closed adaptation of registry predicates to Flow publication guards. */
#include <algorithm>
#include <atlas/ui_content/flow_conditions.hpp>
#include <map>

namespace atlas::ui_content
{
/// @brief Compile edge lookup once, leaving Lua execution on the viewport coordinator.
flow_guard_evaluator make_flow_guard_evaluator(scripting::domain& scripts,
                                               const flow_document& graph,
                                               std::span<const flow_condition_binding> bindings)
{
    if (bindings.size() > 512)
    {
        throw std::length_error("Too many flow condition bindings");
    }

    std::map<std::string, scripting::condition_node, std::less<>> expressions;
    for (const auto& item : bindings)
    {
        mvvm::validate_text(item.transition, 64);
        if (std::ranges::none_of(graph.transitions,
                                 [&](const auto& edge)
                                 {
                                     return edge.id == item.transition;
                                 }) ||
            item.transition.empty() ||
            !expressions.emplace(item.transition, item.expression).second)
        {
            throw std::invalid_argument("Empty or duplicate flow condition edge");
        }
    }

    auto compiled = std::make_shared<const decltype(expressions)>(std::move(expressions));
    return [&scripts, compiled = std::move(compiled)](const flow_transition& edge)
    {
        const auto found = compiled->find(edge.id);
        if (found == compiled->end())
        {
            return true;
        }

        const auto result = scripts.evaluate_conditions(found->second);
        if (result.status == scripting::condition_status::invalid ||
            result.status == scripting::condition_status::failed)
        {
            throw std::runtime_error("Flow condition denied: " + result.diagnostic);
        }

        return result.allowed();
    };
}
} // namespace atlas::ui_content
