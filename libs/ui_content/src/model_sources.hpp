/** @file @brief Resolve authored ViewModel sources into independent runtime instances. */
#pragma once
#include <atlas/ui_content/instance.hpp>
#include <map>

namespace atlas::ui_content::detail
{
using model_map =
    std::map<std::string, std::shared_ptr<mvvm::view_model>, std::less<>>; ///< Named owners.
/// @brief Reuse one parsed schema per source, or invoke the matching native factory per context.
[[nodiscard]] model_map make_models(const document& source, const construction& options);
} // namespace atlas::ui_content::detail
