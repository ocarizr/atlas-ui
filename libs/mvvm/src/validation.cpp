/** @file @brief Bounded identifiers, finite scalars and canonical UTF-8 input validation. */
#include "schema_state.hpp"
#include <atomic>
#include <cmath>
#include <limits>

namespace atlas::mvvm
{
/// @brief Validate borrowed content without copying it into a runtime value.
void validate_text(std::string_view text, std::size_t max_bytes)
{
    if (text.size() > max_bytes)
    {
        throw std::overflow_error("MVVM text byte limit exhausted");
    }

    detail::validate_utf8(text);
}

/// @brief Variant order is the public scalar type order.
value_type type_of(const value& data) noexcept
{
    return static_cast<value_type>(data.index());
}

namespace detail
{
/// @brief Cold-path identity generation must never wrap to a stale live handle.
std::uint64_t next_identity()
{
    static std::atomic<std::uint64_t> next{1};
    auto current = next.load();
    do
    {
        if (current == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("MVVM identities exhausted");
        }
    } while (!next.compare_exchange_weak(current, current + 1));

    return current;
}

/// @brief Identifiers are stable across XML, generated C++ and future editor references.
void validate_name(std::string_view name)
{
    const auto letter = [](unsigned char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    if (name.empty() || name.size() > 128 || !letter(static_cast<unsigned char>(name.front())))
    {
        throw std::invalid_argument("MVVM name requires 1..128 identifier bytes");
    }

    for (const unsigned char c : name)
    {
        if (!letter(c) && !(c >= '0' && c <= '9') && c != '.' && c != '-')
        {
            throw std::invalid_argument("invalid MVVM identifier");
        }
    }
}

/// @brief Validate Unicode scalar sequences without replacement or locale-dependent decoding.
void validate_utf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t point = first;
        unsigned remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            point = first & 0x1fU;
            remaining = 1;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            point = first & 0xfU;
            remaining = 2;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            point = first & 7U;
            remaining = 3;
            minimum = 0x10000;
        }
        else if (first >= 0x80)
        {
            throw std::invalid_argument("invalid MVVM UTF-8");
        }

        if (remaining > text.size() - i)
        {
            throw std::invalid_argument("truncated MVVM UTF-8");
        }

        while (remaining--)
        {
            const auto c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0U) != 0x80U)
            {
                throw std::invalid_argument("invalid MVVM UTF-8 continuation");
            }

            point = (point << 6U) | (c & 0x3fU);
        }

        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) ||
            (point < 32 && point != 9 && point != 10 && point != 13) || point == 0xfffe ||
            point == 0xffff)
        {
            throw std::invalid_argument("MVVM text contains an invalid XML Unicode scalar");
        }
    }
}

/// @brief Boundary values never carry nonfinite doubles or unbounded malformed text.
void validate_value(const value& data, std::size_t max_text)
{
    if (data.valueless_by_exception())
    {
        throw std::invalid_argument("invalid MVVM value");
    }

    if (const auto* number = std::get_if<double>(&data); number && !std::isfinite(*number))
    {
        throw std::invalid_argument("MVVM numbers must be finite");
    }

    if (const auto* text = std::get_if<std::string>(&data))
    {
        if (text->size() > max_text)
        {
            throw std::overflow_error("MVVM text limit exhausted");
        }

        validate_utf8(*text);
    }
}
} // namespace detail
} // namespace atlas::mvvm
