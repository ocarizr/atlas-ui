/** @file @brief Bounded little-endian scalar and sequence codecs for prepared UI. */
#pragma once
#include <atlas/mvvm_content/detail/prepared_binary.hpp>
#include <atlas/ui_content/prepared.hpp>

namespace atlas::ui_content::detail
{
using mvvm_content::detail::binary;             // Shared scalar archive.
using mvvm_content::detail::max_prepared_bytes; // Shared envelope cap.
/// @brief Admission cap for unique typed catalog references.
inline constexpr std::size_t max_prepared_dependencies = 32768;
/// @brief Admission cap for included templates and overrides.
inline constexpr std::size_t max_prepared_materials = 8192;

void validate_prepared(const prepared_view& source); ///< Verify the exact typed dependency closure.
void validate_prepared_mount(std::span<const prepared_dependency> dependencies,
                             const construction& options); ///< Verify mounted authored revisions.
} // namespace atlas::ui_content::detail
