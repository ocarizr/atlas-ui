/** @file @brief Shared curve sampling and scalar reads for playback composition. */
#pragma once
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_content/animation.hpp>

namespace atlas::ui_content::detail
{
double ease(animation_curve curve, std::array<float, 2> controls,
            double fraction); ///< Evaluate normalized time.
bool valid_curve(animation_curve curve, std::array<float, 2> controls); ///< Validate easing data.
float sample(const std::vector<animation_key>& keys,
             animation_time position); ///< Evaluate a curve.
ui::property_value read_animation_value(const ui::element_tree& tree, ui::element_handle target,
                                        ui::property field); ///< Read the committed scalar.
} // namespace atlas::ui_content::detail
