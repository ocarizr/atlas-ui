/** @file @brief Authoring-to-runtime element identity resolution. */
#pragma once
#include <atlas/ui_content/document.hpp>
#include <map>

namespace atlas::ui_content::detail
{
using elements = std::map<std::string, ui::element_handle, std::less<>>; ///< Live owner handles.
/// @brief Build a fresh tree and resolve sibling dependencies after all handles exist.
[[nodiscard]] elements build_tree(ui::element_tree& tree, const document& source);
} // namespace atlas::ui_content::detail
