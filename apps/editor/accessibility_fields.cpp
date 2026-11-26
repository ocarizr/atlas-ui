/** @file @brief Typed semantic inspector fields preserve literal/key exclusivity. */
#include "accessibility_inspector.hpp"
#include <charconv>
#include <cmath>

namespace atlas::editor
{
namespace
{
/// @brief Parse the complete scalar without accepting locale separators, suffixes or overflow.
template <class T> T scalar(std::string_view text)
{
    T result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(static_cast<double>(result)))
    {
        throw std::invalid_argument("Expected a finite scalar in range");
    }

    return result;
}
} // namespace

/// @brief Present one supported semantic field without reading runtime UI state.
std::string accessibility_inspector::value(std::string_view field) const
{
    if (field == "name")
    {
        return draft_.name;
    }

    if (field == "description")
    {
        return draft_.description;
    }

    if (field == "name-key")
    {
        return draft_.name_key;
    }

    if (field == "description-key")
    {
        return draft_.description_key;
    }

    if (field == "value")
    {
        return draft_.value;
    }

    if (field == "invoke-action")
    {
        return std::to_string(draft_.invoke_action);
    }

    if (field == "select-action")
    {
        return std::to_string(draft_.select_action);
    }

    if (field == "number")
    {
        return std::to_string(draft_.number);
    }

    if (field == "minimum")
    {
        return std::to_string(draft_.minimum);
    }

    if (field == "maximum")
    {
        return std::to_string(draft_.maximum);
    }

    throw std::invalid_argument("Unknown accessibility field");
}

/// @brief Update typed draft values while keeping literal and localized names exclusive.
void accessibility_inspector::assign(field_edit edit)
{
    const auto field = edit.field;
    const auto text = edit.value;
    if (field == "name")
    {
        draft_.name = text;
        draft_.name_key.clear();
    }
    else if (field == "description")
    {
        draft_.description = text;
        draft_.description_key.clear();
    }
    else if (field == "name-key")
    {
        draft_.name_key = text;
        draft_.name.clear();
    }
    else if (field == "description-key")
    {
        draft_.description_key = text;
        draft_.description.clear();
    }
    else if (field == "value")
    {
        draft_.value = text;
    }
    else if (field == "invoke-action")
    {
        draft_.invoke_action = scalar<std::uint32_t>(text);
    }
    else if (field == "select-action")
    {
        draft_.select_action = scalar<std::uint32_t>(text);
    }
    else if (field == "number")
    {
        draft_.number = scalar<double>(text);
    }
    else if (field == "minimum")
    {
        draft_.minimum = scalar<double>(text);
    }
    else if (field == "maximum")
    {
        draft_.maximum = scalar<double>(text);
    }
    else
    {
        throw std::invalid_argument("Unknown accessibility field");
    }
}
} // namespace atlas::editor
