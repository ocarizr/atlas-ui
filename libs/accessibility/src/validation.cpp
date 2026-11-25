/** @file @brief Portable semantic invariants and advertised action capabilities. */
#include <algorithm>
#include <atlas/accessibility/channel.hpp>
#include <cmath>
#include <numeric>
#include <tuple>

namespace atlas::accessibility
{
namespace
{
/// @brief Reject embedded NUL, overlong UTF-8, surrogates and non-Unicode codepoints.
bool valid_text(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (!first)
        {
            return false;
        }

        if (first < 128)
        {
            continue;
        }

        unsigned remaining = first >= 0xC2 && first <= 0xDF   ? 1
                             : first >= 0xE0 && first <= 0xEF ? 2
                             : first >= 0xF0 && first <= 0xF4 ? 3
                                                              : 0;
        if (!remaining || text.size() - i < remaining)
        {
            return false;
        }

        std::uint32_t value = first & ((1u << (6 - remaining)) - 1);
        const auto count = remaining;
        while (remaining--)
        {
            const auto part = static_cast<unsigned char>(text[i++]);
            if ((part & 0xC0) != 0x80)
            {
                return false;
            }

            value = (value << 6) | (part & 0x3F);
        }

        if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ||
            (count == 1 && value < 0x80) || (count == 2 && value < 0x800) ||
            (count == 3 && value < 0x10000))
        {
            return false;
        }
    }

    return true;
}
} // namespace

/// @brief Reject unsupported and disabled actions without invoking application code.
bool supports(const node& source, action operation) noexcept
{
    if (!source.enabled)
    {
        return false;
    }

    switch (operation)
    {
        case action::focus:
            return source.focusable;
        case action::invoke:
            return source.invoke;
        case action::select:
            return source.select;
        case action::reveal:
            return source.reveal;
    }

    return false;
}

/// @brief Parent-before-child unique identities make cycles impossible without recursive walks.
void validate(std::span<const node> nodes, limits bounds)
{
    if (nodes.size() > bounds.max_nodes)
    {
        throw std::length_error("accessibility node limit exhausted");
    }

    const auto key = [](identity id)
    {
        return std::tuple{id.session, id.index, id.generation};
    };
    std::vector<std::size_t> order(nodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order,
                      [&](auto left, auto right)
                      {
                          return key(nodes[left].id) < key(nodes[right].id);
                      });
    if (std::ranges::adjacent_find(order,
                                   [&](auto left, auto right)
                                   {
                                       return nodes[left].id == nodes[right].id;
                                   }) != order.end())
    {
        throw std::invalid_argument("duplicate accessibility identity");
    }

    const auto position = [&](identity id)
    {
        const auto found = std::ranges::lower_bound(order, key(id), {},
                                                    [&](auto index)
                                                    {
                                                        return key(nodes[index].id);
                                                    });
        return found != order.end() && nodes[*found].id == id ? *found : nodes.size();
    };
    std::size_t bytes = 0;
    unsigned focused = 0;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto& value = nodes[index];
        if (!value.id.session || !value.id.generation ||
            (value.parent != identity{} && position(value.parent) >= index) ||
            value.kind > role::scroll_container)
        {
            throw std::invalid_argument("invalid accessibility identity or hierarchy");
        }

        const auto valid_bounds = [](const rectangle& box)
        {
            return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) &&
                   std::isfinite(box.height) && box.width >= 0 && box.height >= 0 &&
                   std::isfinite(box.x + box.width) && std::isfinite(box.y + box.height);
        };
        if (!valid_bounds(value.bounds) || (value.clipped && !valid_bounds(value.clip)))
        {
            throw std::invalid_argument("invalid accessibility bounds or clip");
        }

        if (!std::isfinite(value.number) || !std::isfinite(value.minimum) ||
            !std::isfinite(value.maximum) || value.minimum > value.maximum ||
            value.number < value.minimum || value.number > value.maximum)
        {
            throw std::invalid_argument("invalid accessible numeric range");
        }

        for (const auto* text : {&value.name, &value.description, &value.value})
        {
            if (text->size() > bounds.max_text_bytes - bytes)
            {
                throw std::length_error("accessibility text limit exhausted");
            }

            if (!valid_text(*text))
            {
                throw std::invalid_argument("invalid accessibility UTF-8 text");
            }

            bytes += text->size();
        }

        if (value.focused && (!value.focusable || !value.enabled || ++focused > 1))
        {
            throw std::invalid_argument("invalid accessibility focus state");
        }
    }
}
} // namespace atlas::accessibility
