/** @file @brief Shared limits and document-local validation for all authoring inputs. */
#pragma once
#include <atlas/ui_content/document.hpp>

namespace atlas::ui_content::detail
{
inline constexpr std::size_t max_document_bytes = std::size_t{4} * 1024 * 1024; ///< XML ceiling.
inline constexpr std::size_t max_elements = 4096; ///< Maximum UI tree size.
inline constexpr std::size_t max_models = 64;     ///< Maximum simultaneously named models.
inline constexpr std::size_t max_depth = 64;      ///< Maximum UI input route depth.
void validate(const document& source); ///< Validate identities and local descriptor references.
} // namespace atlas::ui_content::detail
