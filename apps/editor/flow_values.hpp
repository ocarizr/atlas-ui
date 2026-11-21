/** @file @brief Exact scalar presentation/parsing shared by action and predicate inspectors. */
#pragma once
#include <array>
#include <atlas/mvvm/schema.hpp>
#include <charconv>
#include <cmath>
#include <limits>

namespace atlas::editor
{
/// @brief Shortest round-trippable decimal text retains the scalar's floating-point identity.
inline std::string flow_number_text(double value)
{
    if (!std::isfinite(value))
    {
        throw std::invalid_argument("Expected a finite numeric value");
    }

    std::array<char, 64> buffer{};
    const auto encoded = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (encoded.ec != std::errc{})
    {
        throw std::invalid_argument("Cannot format numeric value");
    }

    std::string result{buffer.data(), encoded.ptr};
    if (result.find_first_of(".eE") == std::string::npos)
    {
        result += ".0";
    }

    return result;
}

/// @brief Arrow-key stepping exposes negative/fractional values without native text/IME input.
inline std::string step_flow_number(std::string_view text, int direction)
{
    if (direction != 1 && direction != -1)
    {
        throw std::invalid_argument("Numeric step direction must be one or minus one");
    }

    const bool fractional = text.find_first_of(".eE") != std::string_view::npos;
    if (fractional)
    {
        double value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        value += static_cast<double>(direction) * 0.1;
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            !std::isfinite(value))
        {
            throw std::invalid_argument("Cannot step this numeric value");
        }

        return flow_number_text(value);
    }

    std::int64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        (direction > 0 && value == std::numeric_limits<std::int64_t>::max()) ||
        (direction < 0 && value == std::numeric_limits<std::int64_t>::min()))
    {
        throw std::invalid_argument("Cannot step this integer value");
    }

    return std::to_string(value + direction);
}

/// @brief Display owned scalar values without changing their stored type.
inline std::string flow_value(const mvvm::value& value)
{
    return std::visit(
        [](const auto& scalar) -> std::string
        {
            using type = std::remove_cvref_t<decltype(scalar)>; ///< Exact active scalar type.
            if constexpr (std::is_same_v<type, std::string>)
            {
                return scalar;
            }
            else if constexpr (std::is_same_v<type, bool>)
            {
                return scalar ? "true" : "false";
            }
            else if constexpr (std::is_same_v<type, double>)
            {
                return flow_number_text(scalar);
            }
            else
            {
                return std::to_string(scalar);
            }
        },
        value);
}

/// @brief Reject malformed numeric/boolean input without changing a field's schema type.
inline mvvm::value parse_flow_value(const mvvm::value& prototype, std::string_view text)
{
    return std::visit(
        [&](const auto& scalar) -> mvvm::value
        {
            using type = std::remove_cvref_t<decltype(scalar)>; ///< Retain exact argument schema.
            if constexpr (std::is_same_v<type, std::string>)
            {
                return std::string{text};
            }
            else if constexpr (std::is_same_v<type, bool>)
            {
                if (text != "true" && text != "false")
                {
                    throw std::invalid_argument("Expected true or false");
                }

                return text == "true";
            }
            else
            {
                type result{};
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                {
                    throw std::invalid_argument("Expected an exact typed number");
                }

                if constexpr (std::is_same_v<type, double>)
                {
                    if (!std::isfinite(result))
                    {
                        throw std::invalid_argument("Expected a finite numeric value");
                    }
                }

                return result;
            }
        },
        prototype);
}
} // namespace atlas::editor
