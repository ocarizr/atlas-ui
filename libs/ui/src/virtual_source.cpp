/** @file @brief Checked exact fixed and variable list geometry. */
#include <algorithm>
#include <atlas/ui/virtual_source.hpp>
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Never silently collapse adjacent logical positions in float layout coordinates.
float checked_end(float start, float height)
{
    const auto end = start + height;
    if (!std::isfinite(height) || height <= 0 || !std::isfinite(end) || end <= start)
    {
        throw std::invalid_argument("virtual list heights must have distinct finite positive ends");
    }

    return end;
}
} // namespace

/// @brief Reject nonrepresentable logical extents without allocating row metadata.
fixed_item_source::fixed_item_source(fixed_item_options options) : options_(options)
{
    (void)checked_end(0, options.height);
    if (options.count > 16777216 || !std::isfinite(extent()))
    {
        throw std::invalid_argument("fixed virtual list exceeds float coordinate precision");
    }

    if (options.count)
    {
        (void)checked_end(static_cast<float>(options.count - 1) * options.height, options.height);
        if (std::nextafter(extent(), std::numeric_limits<float>::infinity()) - extent() >
            options.height)
        {
            throw std::invalid_argument(
                "fixed virtual rows exceed distinct float endpoint precision");
        }
    }
}

/// @brief Return logical source size independently of physical realization.
std::size_t fixed_item_source::count() const noexcept
{
    return options_.count;
}

/// @brief Read the validated total logical content height.
float fixed_item_source::extent() const noexcept
{
    return static_cast<float>(options_.count) * options_.height;
}

/// @brief Resolve checked logical geometry without creating a widget.
logical_item fixed_item_source::item(std::size_t index) const
{
    if (index >= count())
    {
        throw std::out_of_range("virtual list item index");
    }

    const auto start = static_cast<float>(index) * options_.height;
    const auto end = static_cast<float>(index + 1) * options_.height;
    return {index + 1, start, end - start, 0};
}

/// @brief Resolve a stable key without depending on recycled physical identities.
std::optional<std::size_t> fixed_item_source::find(item_key key) const
{
    return key && key <= count() ? std::optional<std::size_t>{static_cast<std::size_t>(key - 1)}
                                 : std::nullopt;
}

/// @brief Locate the half-open row intersection using immutable extent metadata.
std::size_t fixed_item_source::locate(float position) const
{
    if (!std::isfinite(position))
    {
        throw std::invalid_argument("nonfinite virtual list offset");
    }

    if (position <= 0)
    {
        return 0;
    }

    if (position >= extent())
    {
        return count();
    }

    auto index = std::min(
        count() - 1, static_cast<std::size_t>(static_cast<double>(position) / options_.height));
    while (index && position < item(index).offset)
    {
        --index;
    }

    while (index < count())
    {
        const auto row = item(index);
        if (row.offset + row.height > position)
        {
            break;
        }

        ++index;
    }

    return index;
}

/// @brief Build exact prefix geometry and a unique stable-key index.
variable_item_source::variable_item_source(std::span<const item_description> items,
                                           std::shared_ptr<std::pmr::memory_resource> memory)
    : memory_(memory ? std::move(memory)
                     : std::shared_ptr<std::pmr::memory_resource>{std::pmr::new_delete_resource(),
                                                                  [](std::pmr::memory_resource*)
                                                                  {
                                                                  }}),
      items_(0, memory_.get()), keys_(0, memory_.get())
{
    items_.reserve(items.size());
    keys_.reserve(items.size());
    for (const auto& entry : items)
    {
        const auto end = checked_end(extent_, entry.height);
        if (!entry.key)
        {
            throw std::invalid_argument("virtual item key cannot be zero");
        }

        keys_.emplace_back(entry.key, items_.size());
        items_.push_back({entry.key, extent_, end - extent_, entry.revision});
        extent_ = end;
    }

    std::sort(keys_.begin(), keys_.end());
    if (std::adjacent_find(keys_.begin(), keys_.end(),
                           [](const auto& a, const auto& b)
                           {
                               return a.first == b.first;
                           }) != keys_.end())
    {
        throw std::invalid_argument("duplicate virtual item key");
    }
}

/// @brief Return logical source size independently of physical realization.
std::size_t variable_item_source::count() const noexcept
{
    return items_.size();
}

/// @brief Read the validated total logical content height.
float variable_item_source::extent() const noexcept
{
    return extent_;
}

/// @brief Resolve checked logical geometry without creating a widget.
logical_item variable_item_source::item(std::size_t index) const
{
    return items_.at(index);
}

/// @brief Resolve a stable key without depending on recycled physical identities.
std::optional<std::size_t> variable_item_source::find(item_key key) const
{
    const auto found = std::lower_bound(keys_.begin(), keys_.end(), key,
                                        [](const auto& entry, item_key sought)
                                        {
                                            return entry.first < sought;
                                        });
    return found != keys_.end() && found->first == key ? std::optional<std::size_t>{found->second}
                                                       : std::nullopt;
}

/// @brief Locate the half-open row intersection using immutable extent metadata.
std::size_t variable_item_source::locate(float position) const
{
    if (!std::isfinite(position))
    {
        throw std::invalid_argument("nonfinite virtual list offset");
    }

    const auto found = std::upper_bound(items_.begin(), items_.end(), position,
                                        [](float coordinate, const logical_item& row)
                                        {
                                            return coordinate < row.offset + row.height;
                                        });
    return static_cast<std::size_t>(found - items_.begin());
}
} // namespace atlas::ui
