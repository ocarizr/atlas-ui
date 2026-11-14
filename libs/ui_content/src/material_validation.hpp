/** @file @brief Shared validation for XML and prepared material descriptors. */
#pragma once
#include <atlas/ui_content/material.hpp>

namespace atlas::ui_content::detail
{
/// @brief Bound input bytes for a template or override XML file.
inline constexpr std::size_t max_material_bytes = std::size_t{64} * 1024;
void validate_material_document(const material_document& source); ///< Check either authored form.
} // namespace atlas::ui_content::detail
