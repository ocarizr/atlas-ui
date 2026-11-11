/** @file @brief Owned startup acquisition from the typed asset streamer. */
#pragma once
#include <atlas/ui_content/instance.hpp>

namespace atlas::ui_content::detail
{
/// @brief Read exact bounded UTF-8 bytes and verify the current catalog mount before use.
[[nodiscard]] std::string acquire(assets::asset_info asset, std::size_t maximum,
                                  const construction& options);
} // namespace atlas::ui_content::detail
