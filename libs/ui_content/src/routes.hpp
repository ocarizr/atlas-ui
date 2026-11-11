/** @file @brief Compose model actions and shared Lua behavior behind one UI handler. */
#pragma once
#include "model_sources.hpp"
#include <set>

namespace atlas::ui_content::detail
{
/// @brief Build one per-element registration while retaining declaration order.
[[nodiscard]] std::shared_ptr<ui::input_handler>
make_handler(const element& node, const model_map& models,
             const std::shared_ptr<scripting::domain>& domain,
             std::set<assets::asset_id>& registered, const construction& options);
} // namespace atlas::ui_content::detail
