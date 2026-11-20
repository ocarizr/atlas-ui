/** @file @brief Shared draft bounds and existing-identity lookup. */
#pragma once
#include <atlas/editor_flow/document.hpp>

namespace atlas::editor_flow::detail
{
inline constexpr std::size_t max_bytes =
    std::size_t{2} * 1024 * 1024;                            ///< Complete XML snapshot cap.
bool contains(const document& source, const item_ref& item); ///< Existing stable graph identity.
} // namespace atlas::editor_flow::detail
