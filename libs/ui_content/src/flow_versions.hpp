/** @file @brief Detect authored presentation extensions without changing legacy wire tags. */
#pragma once
#include <algorithm>
#include <atlas/ui_content/flow.hpp>

namespace atlas::ui_content::detail
{
/// @brief Legacy-only graphs retain XML v1 and prepared v1 encoding.
inline bool flow_v2(const flow_document& source)
{
    return std::ranges::any_of(source.regions,
                               [](const auto& item)
                               {
                                   return !item.host.empty() || item.remember;
                               }) ||
           std::ranges::any_of(source.screens,
                               [](const auto& item)
                               {
                                   return item.asset.type != assets::asset_type::ui ||
                                          item.popup != popup_overrides{};
                               }) ||
           std::ranges::any_of(source.transitions,
                               [](const auto& item)
                               {
                                   return item.operation > flow_operation::reload;
                               });
}
} // namespace atlas::ui_content::detail
