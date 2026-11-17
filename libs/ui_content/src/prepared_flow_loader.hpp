/** @file @brief Internal prepared screen adapter for the existing Flow Manager. */
#pragma once
#include <atlas/ui_content/prepared.hpp>

namespace atlas::ui_content::detail
{
/// @brief Bind independently stored UI records to their expected prepared closures.
[[nodiscard]] flow_loader make_prepared_flow_loader(const construction& options,
                                                    std::vector<prepared_flow_screen> screens);
} // namespace atlas::ui_content::detail
