/** @file @brief Exact parameter schemas and bounded namespaced callable metadata. */
#include "callable_state.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace atlas::scripting::detail
{
namespace
{
/// @brief Registry identities are portable dotted names, never paths or executable expressions.
bool identifier(std::string_view value)
{
    return !value.empty() && value.size() <= 128 && value.front() != '.' && value.back() != '.' &&
           value.find("..") == value.npos &&
           std::ranges::all_of(value,
                               [](char ch)
                               {
                                   return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
                                          ch == '.';
                               });
}

/// @brief Generated signatures reject Lua keywords and the injected context name.
bool parameter_symbol(std::string_view name)
{
    constexpr std::array reserved{"and",   "break", "do",       "else",  "elseif", "end",
                                  "false", "for",   "function", "goto",  "if",     "in",
                                  "local", "nil",   "not",      "or",    "repeat", "return",
                                  "then",  "true",  "until",    "while", "context"};
    return identifier(name) && name.find_first_of(".-") == name.npos &&
           !(name.front() >= '0' && name.front() <= '9') &&
           std::ranges::none_of(reserved,
                                [name](const auto* word)
                                {
                                    return name == word;
                                });
}

/// @brief Choices remain integers and reject duplicate labels/values before use.
void choices(const callable_parameter& parameter)
{
    if (parameter.choices.size() > 64 ||
        (!parameter.choices.empty() && parameter.type != mvvm::value_type::integer))
    {
        throw std::invalid_argument("Callable choices require at most 64 integer values");
    }

    std::set<std::int64_t> values;
    std::set<std::string> labels;
    for (const auto& item : parameter.choices)
    {
        mvvm::validate_text(item.label, 128);
        if (item.label.empty() || !values.insert(item.value).second ||
            !labels.insert(item.label).second)
        {
            throw std::invalid_argument("Duplicate or empty callable choice");
        }
    }
}

/// @brief No numeric/string coercion is permitted at a registered call boundary.
void argument(const callable_parameter& parameter, const mvvm::value& value)
{
    if (mvvm::type_of(value) != parameter.type)
    {
        throw std::invalid_argument("Callable parameter type mismatch: " + parameter.name);
    }

    if (!parameter.choices.empty() && std::ranges::none_of(parameter.choices,
                                                           [&](const auto& choice)
                                                           {
                                                               return choice.value ==
                                                                      std::get<std::int64_t>(value);
                                                           }))
    {
        throw std::invalid_argument("Callable argument is not a declared choice");
    }
}
} // namespace

/// @brief Bound copied metadata even when many descriptors reuse the same Lua string.
std::size_t callable_metadata_bytes(const callable_descriptor& descriptor)
{
    std::size_t result = sizeof(descriptor) + descriptor.id.size() + descriptor.label.size() +
                         descriptor.category.size();
    for (const auto& parameter : descriptor.parameters)
    {
        result += sizeof(parameter) + parameter.name.size();
        if (parameter.default_value)
        {
            result += payload_bytes(std::span{&*parameter.default_value, 1});
        }

        for (const auto& choice : parameter.choices)
        {
            result += sizeof(choice) + choice.label.size();
        }
    }

    return result;
}

/// @brief Validate source-independent metadata before either registration or code generation.
void validate_callable_descriptor(const callable_descriptor& descriptor)
{
    mvvm::validate_text(descriptor.label, 128);
    mvvm::validate_text(descriptor.category, 128);
    if (!identifier(descriptor.id) || descriptor.label.empty() || descriptor.category.empty() ||
        descriptor.kind > callable_kind::condition || descriptor.parameters.size() > 32)
    {
        throw std::invalid_argument("Invalid callable descriptor");
    }

    std::set<std::string> names;
    bool optional = false;
    std::size_t default_bytes = 0;
    for (const auto& item : descriptor.parameters)
    {
        if (!parameter_symbol(item.name) || item.type > mvvm::value_type::text ||
            !names.insert(item.name).second || (optional && !item.default_value))
        {
            throw std::invalid_argument("Invalid callable parameter order/name/type");
        }

        optional |= item.default_value.has_value();
        choices(item);
        if (item.default_value)
        {
            validate_values(std::span{&*item.default_value, 1}, 65536);
            argument(item, *item.default_value);
            default_bytes += payload_bytes(std::span{&*item.default_value, 1});
            if (default_bytes > 65536)
            {
                throw std::length_error("Callable default text budget exhausted");
            }
        }
    }
}

/// @brief An origin owns its prefix but cannot silently replace another registered ID.
void validate_callable(const callable_origin& origin, const callable_descriptor& descriptor)
{
    validate_callable_descriptor(descriptor);
    if (!identifier(origin.name) || origin.kind > callable_origin_kind::add_on ||
        !descriptor.id.starts_with(origin.name + "."))
    {
        throw std::invalid_argument("Callable ID is outside its contributor namespace");
    }
}

/// @brief Resolve trailing defaults once before entering native or Lua implementations.
std::vector<mvvm::value> callable_arguments(const callable_descriptor& descriptor,
                                            std::span<const mvvm::value> arguments,
                                            std::size_t max_text)
{
    if (arguments.size() > descriptor.parameters.size())
    {
        throw std::invalid_argument("Too many callable arguments");
    }

    std::vector<mvvm::value> result;
    result.reserve(descriptor.parameters.size());
    for (std::size_t i = 0; i < descriptor.parameters.size(); ++i)
    {
        const auto& parameter = descriptor.parameters[i];
        if (i < arguments.size())
        {
            result.push_back(arguments[i]);
        }
        else if (parameter.default_value)
        {
            result.push_back(*parameter.default_value);
        }
        else
        {
            throw std::invalid_argument("Missing callable argument: " + parameter.name);
        }

        argument(parameter, result.back());
    }

    validate_values(result, max_text);
    return result;
}
} // namespace atlas::scripting::detail
