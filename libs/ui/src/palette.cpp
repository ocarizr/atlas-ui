/** @file @brief Compile bounded semantic tables and publish compatible owner selections. */
#include <algorithm>
#include <atlas/ui/palette.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace atlas::ui
{
/// @brief Restrict keys to portable, case-sensitive identifiers with bounded diagnostic storage.
bool valid_palette_key(std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 64 &&
           std::ranges::all_of(value,
                               [](unsigned char c)
                               {
                                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                          (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                                          c == '.';
                               });
}

/// @brief Validation finishes before a shared immutable allocation is exposed.
std::shared_ptr<const palette> palette::compile(palette_definition source)
{
    if (!valid_palette_key(source.contract) || source.colors.empty() ||
        source.colors.size() > max_palette_colors)
    {
        throw std::invalid_argument("Invalid palette contract or color count");
    }

    std::ranges::sort(source.colors, {}, &palette_color::key);
    std::string_view previous;
    for (const auto& entry : source.colors)
    {
        if (!valid_palette_key(entry.key) || entry.key == previous ||
            !std::ranges::all_of(entry.value,
                                 [](float v)
                                 {
                                     return std::isfinite(v) && v >= 0 && v <= 1;
                                 }))
        {
            throw std::invalid_argument("Invalid or duplicate palette color: " + entry.key);
        }

        previous = entry.key;
    }

    return std::shared_ptr<const palette>{new palette{std::move(source)}};
}

/// @brief The private constructor receives only checked, sorted source data.
palette::palette(palette_definition source) : source_{std::move(source)}
{
}

/// @brief Resolve case-sensitive authoring names once, outside the warmed render path.
palette_slot palette::find(std::string_view key) const
{
    const auto found = std::ranges::lower_bound(source_.colors, key, {}, &palette_color::key);
    if (found == source_.colors.end() || found->key != key)
    {
        throw std::out_of_range("Unknown palette key: " + std::string{key});
    }

    return static_cast<palette_slot>(found - source_.colors.begin());
}

/// @brief Access a resolved slot without hashing or parsing source names.
const rgba& palette::color(palette_slot slot) const
{
    return source_.colors.at(slot).value;
}

/// @brief Native construction and diagnostics share the exact sorted immutable definition.
const palette_definition& palette::definition() const noexcept
{
    return source_;
}

/// @brief No hash collision or nominal contract match can hide a changed key set.
bool palette::compatible(const palette& other) const noexcept
{
    return source_.contract == other.source_.contract &&
           std::ranges::equal(source_.colors, other.source_.colors, {}, &palette_color::key,
                              &palette_color::key);
}

/// @brief Context creation never accepts an uninitialized theme.
palette_context::palette_context(std::shared_ptr<const palette> initial,
                                 diagnostics::services sinks)
    : owner_{std::this_thread::get_id()}, selected_{std::move(initial)}, sinks_{std::move(sinks)}
{
    if (!selected_)
    {
        throw std::invalid_argument("Missing initial palette");
    }
}

/// @brief Reject incorrect ownership before inspecting non-atomic context fields.
void palette_context::require_owner() const
{
    if (owner_ != std::this_thread::get_id())
    {
        throw std::logic_error("Palette context requires its owner thread");
    }
}

/// @brief Validate before a no-throw pointer publication; preserve the previous theme on failure.
void palette_context::select(std::shared_ptr<const palette> candidate)
{
    require_owner();
    const diagnostics::operation_scope scope{sinks_, {"ui.palette.select", "ui.content"}};
    if (!candidate || !selected_->compatible(*candidate))
    {
        diagnostics::write(sinks_, diagnostics::log_level::warning, "ui.palette",
                           "Rejected incompatible palette selection");
        throw std::invalid_argument("Incompatible palette contract or key set");
    }

    if (candidate == selected_)
    {
        return;
    }

    if (revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Palette revision exhausted");
    }

    selected_ = std::move(candidate);
    ++revision_;
}

/// @brief Workers retain immutable snapshots captured by their owner, never this context.
std::shared_ptr<const palette> palette_context::snapshot() const
{
    require_owner();
    return selected_;
}

/// @brief Cheap frame-boundary change detection without comparing entire definitions.
std::uint64_t palette_context::revision() const
{
    require_owner();
    return revision_;
}
} // namespace atlas::ui
