/** @file @brief Portable build adapter with explicit unsupported native-plural diagnostics. */
#include <atlas/localization/catalog.hpp>
#include <stdexcept>

namespace atlas::localization
{
/// @brief Native plural data is not present in this build profile.
bool plural_backend_available() noexcept
{
    return false;
}

/// @brief Inject a supported selector rather than substituting incorrect English rules.
plural_category select_plural(std::string_view, double)
{
    throw std::runtime_error("localization plural backend unavailable");
}
} // namespace atlas::localization
