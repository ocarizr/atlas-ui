/** @file @brief Explicit loss-checked conversion between model scalars and UI properties. */
#include "binding_state.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace atlas::ui_mvvm
{
namespace
{
/// @brief Property domains are checked before any model demand/UI command is published.
void domain(float number, ui::property property)
{
    if (!std::isfinite(number) || number < 0 || (property == ui::property::opacity && number > 1))
    {
        throw std::invalid_argument("MVVM value outside UI property domain");
    }
}

/// @brief UI floats must fit the source integer domain without truncation or overflow.
std::int64_t integer(float number)
{
    const auto widened = static_cast<double>(number);
    if (!std::isfinite(number) || std::trunc(widened) != widened ||
        widened >= 9223372036854775808.0 || widened < -9223372036854775808.0)
    {
        throw std::invalid_argument("UI number cannot be represented as an MVVM integer");
    }

    return static_cast<std::int64_t>(number);
}
} // namespace

/// @brief Numeric bindings reject silent float overflow and integer precision loss.
ui::property_value to_ui(const mvvm::view_model& model, mvvm::field_handle field,
                         ui::property property)
{
    const auto type = model.description().type(field);
    if (property == ui::property::visible)
    {
        if (type != mvvm::value_type::boolean)
        {
            throw std::invalid_argument("UI visibility requires an MVVM boolean");
        }

        return model.boolean(field);
    }

    if (property > ui::property::visible)
    {
        throw std::invalid_argument("unknown UI binding property");
    }

    float result{};
    if (type == mvvm::value_type::integer)
    {
        const auto source = model.integer(field);
        result = static_cast<float>(source);
        if (integer(result) != source)
        {
            throw std::invalid_argument("MVVM integer loses precision in UI binding");
        }
    }
    else if (type == mvvm::value_type::number)
    {
        const auto source = model.number(field);
        if (std::abs(source) > std::numeric_limits<float>::max())
        {
            throw std::invalid_argument("MVVM number overflows UI float");
        }

        result = static_cast<float>(source);
    }
    else
    {
        throw std::invalid_argument("UI numeric property requires an MVVM number/integer");
    }

    domain(result, property);
    return result;
}

/// @brief Reverse binding has no implicit boolean/string conversions or automatic-size sentinel.
mvvm::value to_model(ui::property_value value, mvvm::value_type type, ui::property property)
{
    if (property == ui::property::visible)
    {
        if (type != mvvm::value_type::boolean || !std::holds_alternative<bool>(value))
        {
            throw std::invalid_argument("invalid reverse visibility binding");
        }

        return std::get<bool>(value);
    }

    if (!std::holds_alternative<float>(value))
    {
        throw std::invalid_argument("reverse numeric binding requires an explicit float");
    }

    const auto number = std::get<float>(value);
    domain(number, property);
    if (type == mvvm::value_type::integer)
    {
        return integer(number);
    }

    if (type == mvvm::value_type::number)
    {
        return static_cast<double>(number);
    }

    throw std::invalid_argument("invalid reverse numeric binding type");
}
} // namespace atlas::ui_mvvm
