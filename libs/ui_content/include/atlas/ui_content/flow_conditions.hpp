/**
 * @file
 * @brief Owner-bound action registry conditions for outgoing Flow transitions.
 */
#pragma once
#include <atlas/scripting/domain.hpp>
#include <atlas/ui_content/flow.hpp>

namespace atlas::ui_content
{
/// @brief Runtime binding compiled from authored IDs; never serialize a domain lifetime token.
struct flow_condition_binding
{
    std::string transition;               ///< Exact outgoing edge identity.
    scripting::condition_node expression; ///< Ordered AND/negation expression.
};

/// @brief Borrow the domain and own the bindings; both run only on their common owner thread.
/// Invalid or failed predicates throw, so Flow preserves its previously published screens.
[[nodiscard]] flow_guard_evaluator
make_flow_guard_evaluator(scripting::domain& scripts, const flow_document& graph,
                          std::span<const flow_condition_binding> bindings);
} // namespace atlas::ui_content
