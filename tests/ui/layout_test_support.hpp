/** @file @brief Checked geometry extraction keeps optional failures explicit in every fixture. */
#pragma once
#include <atlas/ui/layout_snapshot.hpp>
#include <stdexcept>

namespace atlas::ui::tests
{
/// @brief Missing geometry fails the fact before any rectangle is dereferenced.
inline layout_geometry require_geometry(const std::optional<layout_geometry>& value)
{
    if (!value)
    {
        throw std::logic_error("expected a complete UI layout geometry value");
    }

    return *value;
}
} // namespace atlas::ui::tests
