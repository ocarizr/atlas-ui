/** @file @brief Typed palette acquisition and generation-checked owner publication. */
#include "storage.hpp"
#include <atlas/ui_content/palette.hpp>

namespace atlas::ui_content
{
/// @brief Reuse bootstrap/staged acquisition and cancellation, never direct filesystem access.
palette_asset load_palette(assets::asset_ref source, const construction& options)
{
    if (source.type != assets::asset_type::palette)
    {
        throw std::invalid_argument("Expected palette asset");
    }

    const auto stamp = options.storage.stamp(source);
    const auto bytes =
        detail::acquire(options.storage.describe(source), max_palette_bytes, options);
    auto value =
        bytes.starts_with("APAL")
            ? decode_palette(std::as_bytes(std::span{bytes}))
            : parse_palette_xml({bytes, assets::format_id(source.id)}, options.diagnostics);
    if (!options.storage.accepts(stamp))
    {
        throw std::runtime_error("Stale palette asset");
    }

    return {stamp, std::move(value)};
}

/// @brief A completed stale read cannot overwrite a theme selected from the current mount.
void publish_palette(ui::palette_context& context, const palette_asset& candidate,
                     const streaming::catalog_streamer& storage)
{
    if (!storage.accepts(candidate.stamp))
    {
        throw std::runtime_error("Stale palette publication");
    }

    context.select(candidate.value);
}
} // namespace atlas::ui_content
