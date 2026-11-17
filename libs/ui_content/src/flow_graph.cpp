/** @file @brief Validate Flow topology, typed guards and bounded structural paths. */
#include <algorithm>
#include <atlas/ui_content/flow.hpp>
#include <cmath>
#include <deque>
#include <map>
#include <set>

namespace atlas::ui_content
{
namespace
{
/// @brief Keep authored names bounded and independent of platform path conventions.
bool valid_id(std::string_view id)
{
    return !id.empty() && id.size() <= 64 &&
           std::ranges::all_of(id,
                               [](char ch)
                               {
                                   return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
                               });
}

/// @brief Validate one exact scalar without coercion.
void validate_value(const mvvm::value& value)
{
    if (const auto* str = std::get_if<std::string>(&value))
    {
        mvvm::validate_text(*str, 4096);
    }

    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
    {
        throw std::invalid_argument("Nonfinite flow parameter");
    }
}

/// @brief Index declarations and reject malformed or duplicate names.
template <class T> auto index(const std::vector<T>& records)
{
    std::map<std::string_view, const T*, std::less<>> result;
    for (const auto& record : records)
    {
        if (!valid_id(record.id) || !result.emplace(record.id, &record).second)
        {
            throw std::invalid_argument("Invalid or duplicate flow ID: " + record.id);
        }
    }

    return result;
}

/// @brief Return a checked declaration rather than exposing an internal map lookup error.
template <class Index> const auto& require(const Index& values, const std::string& id)
{
    const auto found = values.find(id);
    if (found == values.end())
    {
        throw std::invalid_argument("Unknown flow ID: " + id);
    }

    return *found->second;
}

/// @brief Follow only explicit forward edges and implicit nested initial activation.
auto walk(const flow_document& source, std::string_view from, bool check_guards)
{
    using path = std::vector<std::string>; ///< Transition IDs in execution order.
    std::map<std::string_view, path> visited{{from, {}}};
    std::deque<std::string_view> pending{from};
    const auto parameters = index(source.parameters);
    const auto add = [&](std::string_view target, const path& history, std::string_view edge)
    {
        const auto length = history.size() + (edge.empty() ? 0U : 1U);
        const auto found = visited.find(target);
        if (found != visited.end() && found->second.size() <= length)
        {
            return;
        }

        auto& path = visited.insert_or_assign(target, history).first->second;
        if (edge.empty())
        {
            pending.push_front(target);
        }
        else
        {
            path.emplace_back(edge);
            pending.push_back(target);
        }
    };
    while (!pending.empty())
    {
        const auto current = pending.front();
        pending.pop_front();
        const auto& history = visited.at(current);
        for (const auto& region : source.regions)
        {
            if (region.owner == current)
            {
                add(region.initial, history, {});
            }
        }

        for (const auto& edge : source.transitions)
        {
            if (edge.from == current && !edge.to.empty() &&
                (!check_guards ||
                 (edge.enabled &&
                  (!edge.guard ||
                   require(parameters, edge.guard->parameter).value == edge.guard->equals))))
            {
                add(edge.to, history, edge.id);
            }
        }
    }

    return visited;
}
} // namespace

/// @brief Validate ownership, declaration identity and structural reachability.
void validate_flow(const flow_document& source)
{
    if (source.regions.empty() || source.regions.size() > 64 || source.screens.empty() ||
        source.screens.size() > 256 || source.transitions.size() > 1024 ||
        source.parameters.size() > 64)
    {
        throw std::invalid_argument("Flow document exceeds its declaration bounds");
    }

    const auto regions = index(source.regions);
    const auto screens = index(source.screens);
    const auto parameters = index(source.parameters);
    (void)index(source.transitions);
    const flow_region* root = nullptr;
    for (const auto& region : source.regions)
    {
        if (require(screens, region.initial).region != region.id)
        {
            throw std::invalid_argument("Initial screen must belong to its flow region");
        }

        if (region.owner.empty())
        {
            if (root)
            {
                throw std::invalid_argument("Flow requires exactly one viewport region");
            }

            root = &region;
        }
        else
        {
            (void)require(screens, region.owner);
        }

        const auto* parent = &region;
        for (std::size_t depth = 0; !parent->owner.empty(); ++depth)
        {
            if (depth >= 8)
            {
                throw std::invalid_argument("Cyclic or excessively nested flow regions");
            }

            parent = &require(regions, require(screens, parent->owner).region);
        }
    }

    for (const auto& screen : source.screens)
    {
        (void)require(regions, screen.region);
        if (screen.asset.type != assets::asset_type::ui ||
            (!screen.focus.empty() && !valid_id(screen.focus)))
        {
            throw std::invalid_argument("Flow screen requires a UI asset and valid focus ID");
        }

        (void)assets::format_id(screen.asset.id);
    }

    for (const auto& parameter : source.parameters)
    {
        validate_value(parameter.value);
    }

    for (const auto& edge : source.transitions)
    {
        const auto& from = require(screens, edge.from);
        const bool forward = edge.operation == flow_operation::push ||
                             edge.operation == flow_operation::replace ||
                             edge.operation == flow_operation::overlay;
        if (edge.operation > flow_operation::reload || forward == edge.to.empty() ||
            (forward && edge.from == edge.to) ||
            (forward && require(screens, edge.to).region != from.region) ||
            (!edge.control.empty() && !valid_id(edge.control)) ||
            (edge.control.empty() && edge.action != 0))
        {
            throw std::invalid_argument("Invalid flow transition: " + edge.id);
        }

        if (edge.guard)
        {
            validate_value(edge.guard->equals);
            if (mvvm::type_of(require(parameters, edge.guard->parameter).value) !=
                mvvm::type_of(edge.guard->equals))
            {
                throw std::invalid_argument("Flow guard type differs from its parameter");
            }
        }
    }

    if (!root || walk(source, root->initial, false).size() != source.screens.size())
    {
        throw std::invalid_argument(
            "Flow contains screens unreachable from the viewport initial screen");
    }
}

/// @brief Keep automated traversal tied to a declared initial guard fixture.
std::vector<std::string> plan_flow_path(const flow_document& source, std::string_view from,
                                        std::string_view to)
{
    validate_flow(source);
    const auto screens = index(source.screens);
    if (!screens.contains(from) || !screens.contains(to))
    {
        throw std::invalid_argument("Unknown flow path endpoint");
    }

    const auto paths = walk(source, from, true);
    const auto found = paths.find(to);
    if (found == paths.end())
    {
        throw std::out_of_range("No forward flow path under declared guard values");
    }

    return found->second;
}

/// @brief Report declared terminal screens independently of dynamic guard values.
std::vector<std::string> flow_dead_ends(const flow_document& source)
{
    validate_flow(source);
    std::vector<std::string> result;
    for (const auto& screen : source.screens)
    {
        if (std::ranges::none_of(source.transitions,
                                 [&](const auto& edge)
                                 {
                                     return edge.from == screen.id && edge.enabled &&
                                            edge.operation != flow_operation::reload;
                                 }))
        {
            result.push_back(screen.id);
        }
    }

    return result;
}
} // namespace atlas::ui_content
