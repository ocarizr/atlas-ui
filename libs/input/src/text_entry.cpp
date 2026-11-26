/** @file @brief Unicode scalar validation and bounded append-only inspector drafts. */
#include <array>
#include <atlas/input/text_entry.hpp>
#include <stdexcept>

namespace atlas::input
{
/// @brief Reject a zero byte budget before accepting any draft.
text_entry::text_entry(std::size_t max_bytes) : max_bytes_{max_bytes}
{
    if (!max_bytes)
    {
        throw std::invalid_argument("text entry needs a positive capacity");
    }
}

/// @brief Reuse scalar admission to reject overlong, surrogate and truncated UTF-8 source text.
void text_entry::begin(std::string_view value)
{
    if (value.size() > max_bytes_)
    {
        throw std::length_error("text entry capacity exceeded");
    }

    text_entry candidate{max_bytes_};
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i++]);
        std::size_t extra = 0;
        char32_t scalar = lead;
        char32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf)
        {
            extra = 1;
            scalar = lead & 0x1f;
            minimum = 0x80;
        }
        else if (lead >= 0xe0 && lead <= 0xef)
        {
            extra = 2;
            scalar = lead & 0xf;
            minimum = 0x800;
        }
        else if (lead >= 0xf0 && lead <= 0xf4)
        {
            extra = 3;
            scalar = lead & 7;
            minimum = 0x10000;
        }
        else if (lead >= 0x80)
        {
            throw std::invalid_argument("invalid UTF-8 text entry");
        }

        for (std::size_t j = 0; j < extra; ++j)
        {
            if (i == value.size() || (static_cast<unsigned char>(value[i]) & 0xc0) != 0x80)
            {
                throw std::invalid_argument("invalid UTF-8 text entry");
            }

            scalar = (scalar << 6) | (static_cast<unsigned char>(value[i++]) & 0x3f);
        }

        if (scalar < minimum || !candidate.append(scalar))
        {
            throw std::invalid_argument("invalid single-line text entry");
        }
    }

    text_ = std::move(candidate.text_);
}

/// @brief Admit a complete printable scalar only when its UTF-8 bytes fit.
bool text_entry::append(char32_t scalar)
{
    if (scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f) || scalar > 0x10ffff ||
        (scalar >= 0xd800 && scalar <= 0xdfff))
    {
        return false;
    }

    std::array<char, 4> encoded{};
    std::size_t count = 1;
    if (scalar < 0x80)
    {
        encoded[0] = static_cast<char>(scalar);
    }
    else
    {
        count = scalar < 0x800 ? 2U : scalar < 0x10000 ? 3U : 4U;
        auto rest = scalar;
        for (auto i = count - 1; i > 0; --i)
        {
            encoded[i] = static_cast<char>(0x80 | (rest & 0x3f));
            rest >>= 6;
        }

        encoded[0] = static_cast<char>((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | rest);
    }

    if (count > max_bytes_ - text_.size())
    {
        return false;
    }

    text_.append(encoded.data(), count);
    return true;
}

/// @brief Remove the last Unicode scalar without leaving continuation bytes.
void text_entry::backspace()
{
    if (text_.empty())
    {
        return;
    }

    auto first = text_.size() - 1;
    while (first && (static_cast<unsigned char>(text_[first]) & 0xc0) == 0x80)
    {
        --first;
    }

    text_.resize(first);
}

/// @brief Borrow the current private draft without copying or allocating.
std::string_view text_entry::value() const noexcept
{
    return text_;
}
} // namespace atlas::input
