/** @file @brief Windows ICU cardinal categories behind an SDK-free portable contract. */
#include <atlas/localization/catalog.hpp>
#include <cmath>
#include <icu.h>
#include <memory>
#include <stdexcept>

namespace atlas::localization
{
/// @brief This adapter requires the Windows 10 1903 combined ICU C API.
bool plural_backend_available() noexcept
{
    return true;
}

/// @brief Use native CLDR data and release ICU rules on every successful or failing request.
plural_category select_plural(std::string_view locale, double number)
{
    if (!std::isfinite(number))
    {
        throw std::invalid_argument("nonfinite plural count");
    }

    const auto tag = normalize_tag(locale);
    UErrorCode status = U_ZERO_ERROR;
    std::array<char, 128> name{};
    int32_t parsed = 0;
    (void)uloc_forLanguageTag(tag.c_str(), name.data(), static_cast<int32_t>(name.size()), &parsed,
                              &status);
    if (U_FAILURE(status) || parsed != static_cast<int32_t>(tag.size()))
    {
        throw std::invalid_argument("ICU rejected language tag");
    }

    std::unique_ptr<UPluralRules, decltype(&uplrules_close)> rules{
        uplrules_open(name.data(), &status), uplrules_close};
    if (U_FAILURE(status) || !rules)
    {
        throw std::runtime_error("ICU plural rules unavailable");
    }

    std::array<UChar, 16> keyword{};
    const auto size = uplrules_select(rules.get(), number, keyword.data(),
                                      static_cast<int32_t>(keyword.size()), &status);
    if (U_FAILURE(status) || size < 0 || size >= static_cast<int32_t>(keyword.size()))
    {
        throw std::runtime_error("ICU plural selection failed");
    }

    std::string value;
    for (int32_t i = 0; i < size; ++i)
    {
        value.push_back(static_cast<char>(keyword[static_cast<std::size_t>(i)]));
    }

    constexpr std::array names{"zero", "one", "two", "few", "many", "other"};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (value == names[i])
        {
            return static_cast<plural_category>(i);
        }
    }

    throw std::runtime_error("unknown ICU plural category");
}
} // namespace atlas::localization
