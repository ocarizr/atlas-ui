/** @file @brief Preflight every branch before evaluating deterministic bounded AND/NOT groups. */
#include "domain_state.hpp"

namespace atlas::scripting
{
/// @brief A skipped or negated branch cannot conceal an unknown/stale condition.
condition_evaluation domain::evaluate_conditions(const condition_node& expression)
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.conditions", "script.cpu"}};
    std::size_t nodes = 0;
    const auto validate = [&](const auto& self, const condition_node& node,
                              std::size_t depth) -> void
    {
        if (++nodes > 64 || depth > 8)
        {
            throw std::length_error("Condition group depth/node budget exhausted");
        }

        if (node.binding.sequence)
        {
            if (!node.children.empty())
            {
                throw std::invalid_argument("A condition leaf cannot contain children");
            }

            const auto& entry = impl_->callable(node.binding);
            const auto& context = impl_->callable_context(node.binding);
            if (context.failed || !impl_->frame || context.model->inspect().failed ||
                context.model->inspect().frame != impl_->frame)
            {
                throw std::logic_error("Condition requires a healthy matching owner/model frame");
            }

            if (entry.descriptor.kind != callable_kind::condition)
            {
                throw std::invalid_argument("An action cannot be used as a navigation condition");
            }

            (void)detail::callable_arguments(entry.descriptor, node.arguments,
                                             impl_->limits.max_text_bytes);
        }
        else
        {
            if (node.binding != callable_binding{} || node.children.empty() ||
                !node.arguments.empty())
            {
                throw std::invalid_argument(
                    "Condition groups require nonempty children and no arguments");
            }

            for (const auto& child : node.children)
            {
                self(self, child, depth + 1);
            }
        }
    };
    try
    {
        validate(validate, expression, 0);
    }
    catch (const std::exception& error)
    {
        return {condition_status::invalid, error.what()};
    }

    const auto evaluate = [&](const auto& self, const condition_node& node) -> bool
    {
        bool result = true;
        if (node.binding.sequence)
        {
            result = impl_->run_condition(node.binding, node.arguments);
        }
        else
        {
            for (const auto& child : node.children)
            {
                if (!self(self, child))
                {
                    result = false;
                    break;
                }
            }
        }

        return node.negate ? !result : result;
    };
    try
    {
        return {evaluate(evaluate, expression) ? condition_status::passed
                                               : condition_status::blocked,
                {}};
    }
    catch (const std::exception& error)
    {
        diagnostics::write(impl_->destinations, diagnostics::log_level::warning,
                           "scripting.conditions", error.what());
        return {condition_status::failed, error.what()};
    }
}
} // namespace atlas::scripting
