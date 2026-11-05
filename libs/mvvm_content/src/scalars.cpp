/** @file @brief Canonical scalar spelling and bounded public authoring payloads. */
#include "content_state.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace atlas::mvvm_content::detail
{
/// @brief Input limits prevent parser/codegen allocation from following unbounded content.
void check_limits(content_limits limits)
{
    if (!limits.max_bytes || limits.max_bytes > 16777216)
    {
        throw std::invalid_argument("invalid MVVM content byte limit");
    }
}

/// @brief Count large text/name data before schema compilation copies it.
void check_definition(const mvvm::definition& source, content_limits limits)
{
    check_limits(limits);
    std::size_t used = 0;
    const auto account = [&](std::string_view text)
    {
        if (text.size() > limits.max_bytes - used)
        {
            throw std::overflow_error("MVVM content byte limit exhausted");
        }

        used += text.size();
    };
    account(source.name);
    for (const auto& field : source.fields)
    {
        account(field.name);
        if (const auto* text = std::get_if<std::string>(&field.initial))
        {
            account(*text);
        }
    }

    for (const auto* signals : {&source.actions, &source.events})
    {
        for (const auto& signal : *signals)
        {
            account(signal.name);
            for (const auto& parameter : signal.parameters)
            {
                account(parameter.name);
            }
        }
    }

    (void)mvvm::compile(source, limits.schema);
}

/// @brief Stable XML type vocabulary is versioned independently of C++ variant layout.
std::string_view type_name(mvvm::value_type type)
{
    switch (type)
    {
        case mvvm::value_type::boolean:
            return "boolean";
        case mvvm::value_type::integer:
            return "integer";
        case mvvm::value_type::number:
            return "number";
        case mvvm::value_type::text:
            return "text";
    }

    throw std::invalid_argument("unknown MVVM scalar type");
}

/// @brief Unknown types are errors, never strings by fallback.
mvvm::value_type parse_type(std::string_view name)
{
    if (name == "boolean")
    {
        return mvvm::value_type::boolean;
    }

    if (name == "integer")
    {
        return mvvm::value_type::integer;
    }

    if (name == "number")
    {
        return mvvm::value_type::number;
    }

    if (name == "text")
    {
        return mvvm::value_type::text;
    }

    throw std::invalid_argument("unknown MVVM XML type");
}

/// @brief Parsing consumes the whole attribute and never depends on the current locale.
mvvm::value parse_value(mvvm::value_type type, std::string_view text)
{
    if (type == mvvm::value_type::text)
    {
        return std::string{text};
    }

    if (type == mvvm::value_type::boolean)
    {
        if (text == "true")
        {
            return true;
        }

        if (text == "false")
        {
            return false;
        }

        throw std::invalid_argument("invalid MVVM boolean");
    }

    if (type == mvvm::value_type::integer)
    {
        std::int64_t result{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        {
            throw std::invalid_argument("invalid MVVM integer");
        }

        return result;
    }

    double result{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(result))
    {
        throw std::invalid_argument("invalid MVVM number");
    }

    return result;
}

/// @brief max_digits10 preserves every finite authored double through an XML round trip.
std::string value_text(const mvvm::value& data)
{
    if (const auto* text = std::get_if<std::string>(&data))
    {
        return *text;
    }

    if (const auto* boolean = std::get_if<bool>(&data))
    {
        return *boolean ? "true" : "false";
    }

    std::array<char, 128> buffer{};
    std::to_chars_result result;
    if (const auto* integer = std::get_if<std::int64_t>(&data))
    {
        result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), *integer);
    }
    else
    {
        result =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::get<double>(data),
                          std::chars_format::general, std::numeric_limits<double>::max_digits10);
    }

    if (result.ec != std::errc{})
    {
        throw std::overflow_error("MVVM scalar formatting failed");
    }

    return {buffer.data(), result.ptr};
}
} // namespace atlas::mvvm_content::detail
