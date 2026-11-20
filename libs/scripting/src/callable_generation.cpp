/** @file @brief Source-only category and entrypoint generation with stable catalog imports. */
#include "callable_state.hpp"
#include <array>
#include <charconv>
#include <limits>
#include <set>

namespace atlas::scripting
{
namespace
{
/// @brief Quote arbitrary valid text without inserting executable Lua syntax.
std::string literal(std::string_view text)
{
    std::string result{"\""};
    for (const auto ch : text)
    {
        const auto value = static_cast<unsigned char>(ch);
        if (value < 32 || value == 127 || ch == '"' || ch == '\\')
        {
            result += '\\';
            result += static_cast<char>('0' + value / 100);
            result += static_cast<char>('0' + value / 10 % 10);
            result += static_cast<char>('0' + value % 10);
        }
        else
        {
            result += ch;
        }
    }

    return result + '"';
}

/// @brief Preserve integer/float distinction when emitting Lua numeric literals.
std::string scalar(const mvvm::value& value)
{
    if (const auto* text = std::get_if<std::string>(&value))
    {
        return literal(*text);
    }

    if (const auto* logical = std::get_if<bool>(&value))
    {
        return *logical ? "true" : "false";
    }

    if (const auto* integer = std::get_if<std::int64_t>(&value);
        integer && *integer == std::numeric_limits<std::int64_t>::min())
    {
        return "(-9223372036854775807 - 1)";
    }

    std::array<char, 64> buffer{};
    std::to_chars_result formatted;
    const bool floating = std::holds_alternative<double>(value);
    if (floating)
    {
        formatted =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::get<double>(value));
    }
    else
    {
        formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                  std::get<std::int64_t>(value));
    }

    if (formatted.ec != std::errc{})
    {
        throw std::invalid_argument("Cannot encode callable default");
    }

    std::string result{buffer.data(), formatted.ptr};
    if (floating && result.find_first_of(".eE") == result.npos)
    {
        result += ".0";
    }

    return result;
}

/// @brief LuaLS uses number for float; descriptors retain the authored float spelling.
std::string_view type_name(mvvm::value_type type, bool annotation)
{
    switch (type)
    {
        case mvvm::value_type::boolean:
            return "boolean";
        case mvvm::value_type::integer:
            return "integer";
        case mvvm::value_type::number:
            return annotation ? "number" : "float";
        case mvvm::value_type::text:
            return "string";
    }

    throw std::invalid_argument("Unknown callable scalar type");
}

/// @brief Generate a complete typed registration while leaving implementation behavior to the
/// project.
std::string definition(const callable_descriptor& source)
{
    detail::validate_callable_descriptor(source);
    const bool condition = source.kind == callable_kind::condition;
    std::string result = condition ? "navigation_conditions.register({\n" : "actions.register({\n";
    result += "    id = " + literal(source.id) + ",\n    label = " + literal(source.label) +
              ",\n    category = " + literal(source.category) + ",\n    parameters = {\n";
    for (const auto& item : source.parameters)
    {
        result += "        { name = " + literal(item.name) +
                  ", type = " + literal(type_name(item.type, false));
        if (item.default_value)
        {
            result += ", default = " + scalar(*item.default_value);
        }

        if (!item.choices.empty())
        {
            result += ", choices = {";
            for (const auto& choice : item.choices)
            {
                result +=
                    "{label=" + literal(choice.label) + ", value=" + scalar(choice.value) + "},";
            }

            result += "}";
        }

        result += " },\n";
    }

    result += "    },\n    ---@param context atlas_callable_context\n";
    for (const auto& item : source.parameters)
    {
        result +=
            "    ---@param " + item.name + " " + std::string{type_name(item.type, true)} + "\n";
    }

    if (condition)
    {
        result += "    ---@return boolean\n";
    }

    result += condition ? "    evaluate = function(context" : "    execute = function(context";
    for (const auto& item : source.parameters)
    {
        result += ", " + item.name;
    }

    result += ")\n        -- Define project behavior here.\n";
    if (condition)
    {
        result += "        return false\n";
    }

    return result + "    end,\n})\n";
}
} // namespace

/// @brief Caller-owned persistence writes the result through its authorized project write service.
std::string generate_callable_category(std::span<const callable_descriptor> definitions)
{
    if (definitions.empty() || definitions.size() > 128)
    {
        throw std::invalid_argument("Callable category requires 1 to 128 definitions");
    }

    std::string result = "-- Project-owned callable category.\n";
    std::set<std::string> ids;
    for (const auto& item : definitions)
    {
        if (!ids.insert(item.id).second)
        {
            throw std::invalid_argument("Duplicate generated callable ID");
        }

        result += definition(item) + "\n";
        if (result.size() > 262144)
        {
            throw std::length_error("Generated callable category exceeds source budget");
        }
    }

    return result + "return {}\n";
}

/// @brief Module IDs are catalog identities, never filesystem paths selected by Lua.
std::string generate_callable_entrypoint(std::span<const std::string> sources)
{
    if (sources.empty() || sources.size() > 256)
    {
        throw std::invalid_argument("Callable entrypoint requires 1 to 256 categories");
    }

    std::string result = "-- Project-owned callable imports.\n";
    std::set<std::string> ids;
    for (const auto& source : sources)
    {
        mvvm::validate_text(source, 128);
        if (source.empty() || !ids.insert(source).second)
        {
            throw std::invalid_argument("Duplicate or empty callable source identity");
        }

        result += "require(" + literal(source) + ")\n";
    }

    return result + "return {}\n";
}
} // namespace atlas::scripting
