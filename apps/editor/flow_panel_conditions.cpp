/** @file @brief Ordered outgoing AND/NOT decorators with scalar predicate inspectors. */
#include "flow_panel.hpp"
#include "flow_values.hpp"
#include <algorithm>

namespace atlas::editor
{
namespace
{
using condition_path = std::vector<std::size_t>; ///< Persistent within one inspector revision.

/// @brief Resolve only against the current route revision; missing paths fail visibly.
editor_flow::authored_condition& at(editor_flow::document& source, std::string_view route,
                                    const condition_path& path)
{
    const auto found = std::ranges::find(source.conditions, route,
                                         &editor_flow::transition_conditions::transition);
    if (found == source.conditions.end())
    {
        throw std::invalid_argument("Condition group no longer exists");
    }

    auto* node = &found->expression;
    for (auto index : path)
    {
        if (index >= node->children.size())
        {
            throw std::invalid_argument("Condition path is stale");
        }

        node = &node->children[index];
    }

    return *node;
}
} // namespace

/// @brief Render ordered nested groups with explicit negation, context and scalar arguments.
void flow_panel::inspect_conditions(std::vector<control>& rows, const std::string& route)
{
    button(rows, predicate_.empty() ? "Choose predicate | next" : "Predicate: " + predicate_,
           "predicate");
    const auto& source = document_.edit().current();
    const auto found = std::ranges::find(source.conditions, route,
                                         &editor_flow::transition_conditions::transition);
    if (found == source.conditions.end())
    {
        operation(rows, "Add outgoing AND conditions", "condition_root",
                  [this, route = std::string{route}](const auto&)
                  {
                      auto copy = document_.edit().current();
                      copy.conditions.push_back({route, {}});
                      document_.edit().apply(std::move(copy));
                  });
        return;
    }

    const auto visit = [&](auto&& self, const editor_flow::authored_condition& node,
                           const condition_path& path, const std::string& key) -> void
    {
        const auto name =
            std::string(path.size(), '>') + (node.callable.empty() ? "AND group" : node.callable);
        operation(rows, (node.negate ? "NOT " : "") + name, "negate_" + key,
                  [this, route = std::string{route}, path = condition_path{path}](const auto&)
                  {
                      auto copy = document_.edit().current();
                      auto& node = at(copy, route, path);
                      node.negate = !node.negate;
                      document_.edit().apply(std::move(copy));
                  });
        if (node.callable.empty())
        {
            operation(rows, "Add AND child", "group_" + key,
                      [this, route = std::string{route}, path = condition_path{path}](const auto&)
                      {
                          auto copy = document_.edit().current();
                          at(copy, route, path).children.emplace_back();
                          document_.edit().apply(std::move(copy));
                      });
            operation(rows, "Add chosen predicate", "leaf_" + key,
                      [this, route = std::string{route}, path = condition_path{path}](const auto&)
                      {
                          const auto descriptor = std::ranges::find(
                              descriptors_, predicate_, &scripting::callable_descriptor::id);
                          if (descriptor == descriptors_.end() ||
                              descriptor->kind != scripting::callable_kind::condition)
                          {
                              throw std::invalid_argument("Choose a registered predicate first");
                          }

                          auto copy = document_.edit().current();
                          at(copy, route, path)
                              .children.push_back({predicate_, "state",
                                                   editor_flow::default_arguments(*descriptor)});
                          document_.edit().apply(std::move(copy));
                      });
        }
        else
        {
            inspect_condition_values(rows, route, node, path, key);
        }

        inspect_condition_remove(rows, route, path, key, name);
        for (std::size_t i = 0; i < node.children.size(); ++i)
        {
            auto child = path;
            child.push_back(i);
            self(self, node.children[i], std::move(child), key + "_" + std::to_string(i));
        }
    };
    visit(visit, found->expression, {}, "root");
}

/// @brief Edit leaf contexts and schema-checked scalar arguments separately from group structure.
void flow_panel::inspect_condition_values(std::vector<control>& rows, const std::string& route,
                                          const editor_flow::authored_condition& node,
                                          const std::vector<std::size_t>& path,
                                          const std::string& key)
{
    field(rows, "context_" + key, "Model context", node.context,
          [this, route = std::string{route}, path = condition_path{path}](std::string value)
          {
              auto copy = document_.edit().current();
              at(copy, route, path).context = std::move(value);
              document_.edit().apply(std::move(copy));
          });
    for (std::size_t i = 0; i < node.arguments.size(); ++i)
    {
        field(rows, "condition_arg_" + key + std::to_string(i), "Argument " + std::to_string(i),
              flow_value(node.arguments[i]),
              [this, route = std::string{route}, path = condition_path{path},
               i](const std::string& value)
              {
                  auto copy = document_.edit().current();
                  auto& node = at(copy, route, path);
                  node.arguments.at(i) = parse_flow_value(node.arguments.at(i), value);
                  const auto descriptor = std::ranges::find(descriptors_, node.callable,
                                                            &scripting::callable_descriptor::id);
                  if (descriptor == descriptors_.end())
                  {
                      throw std::invalid_argument("Predicate descriptor is unavailable");
                  }

                  editor_flow::validate_arguments(*descriptor, node.arguments);
                  document_.edit().apply(std::move(copy));
              });
    }
}

/// @brief Remove one subtree or the complete outgoing decorator in a single history revision.
void flow_panel::inspect_condition_remove(std::vector<control>& rows, const std::string& route,
                                          const std::vector<std::size_t>& path,
                                          const std::string& key, const std::string& name)
{
    operation(rows, "Remove " + name, "remove_" + key,
              [this, route = std::string{route}, path = condition_path{path}](const auto&)
              {
                  auto copy = document_.edit().current();
                  if (path.empty())
                  {
                      std::erase_if(copy.conditions,
                                    [&](const auto& v)
                                    {
                                        return v.transition == route;
                                    });
                  }
                  else
                  {
                      auto parent = path;
                      const auto index = parent.back();
                      parent.pop_back();
                      auto& children = at(copy, route, parent).children;
                      if (index >= children.size())
                      {
                          throw std::invalid_argument("Condition path is stale");
                      }

                      children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
                  }

                  document_.edit().apply(std::move(copy));
              });
}
} // namespace atlas::editor
