/** @file @brief Deterministic schema validation and immutable lookup preparation. */
#include "state.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace atlas::localization
{
/// @brief Validate a bounded ASCII language tag before normalizing casing.
std::string normalize_tag(std::string_view tag)
{
    if (tag.size() < 2 || tag.size() > 63)
    {
        throw std::invalid_argument("invalid localization language tag");
    }

    std::string result;
    unsigned segment = 0;
    bool first = true;
    for (unsigned char ch : tag)
    {
        if (ch == '-')
        {
            if (!segment || (first && segment < 2))
            {
                throw std::invalid_argument("invalid language subtag");
            }

            segment = 0;
            first = false;
        }
        else
        {
            const bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
            if ((!letter && (first || ch < '0' || ch > '9')) || ++segment > 8)
            {
                throw std::invalid_argument("invalid language subtag");
            }
        }

        result.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch));
    }

    if (!segment)
    {
        throw std::invalid_argument("empty language subtag");
    }

    return result;
}
} // namespace atlas::localization

namespace atlas::localization::detail
{
/// @brief Reject malformed scalar encodings before XML or schema publication.
void validate_text(std::string_view value, std::size_t limit)
{
    if (value.size() > limit)
    {
        throw std::invalid_argument("localization text budget exceeded");
    }

    for (std::size_t i = 0; i < value.size();)
    {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first > 0 && first < 128)
        {
            continue;
        }

        unsigned extra = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            extra = 1;
            code = first & 31U;
            minimum = 128;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            extra = 2;
            code = first & 15U;
            minimum = 2048;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            extra = 3;
            code = first & 7U;
            minimum = 65536;
        }
        else
        {
            throw std::invalid_argument("invalid localization UTF-8");
        }

        if (extra > value.size() - i)
        {
            throw std::invalid_argument("truncated localization UTF-8");
        }

        for (unsigned part = 0; part < extra; ++part)
        {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0U) != 0x80U)
            {
                throw std::invalid_argument("invalid UTF-8 continuation");
            }

            code = (code << 6U) | (next & 63U);
        }

        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        {
            throw std::invalid_argument("invalid UTF-8 scalar");
        }
    }
}

namespace
{
/// @brief Validate message variants and accumulate a total decoded byte budget.
void validate_message(const message& value, std::size_t& bytes)
{
    validate_text(value.key, 128);
    if (value.key.empty())
    {
        throw std::invalid_argument("empty localization key");
    }

    bool plural = false;
    for (const auto& form : value.forms)
    {
        if (form)
        {
            plural = true;
            validate_text(*form, 16384);
            bytes += form->size();
        }
    }

    if (value.text)
    {
        validate_text(*value.text, 16384);
        bytes += value.text->size();
    }

    if (value.text.has_value() == plural || (plural && !value.forms[5]))
    {
        throw std::invalid_argument("message requires literal or plural with other");
    }

    bytes += value.key.size();
    if (bytes > std::size_t{8} * 1024 * 1024)
    {
        throw std::invalid_argument("localization decoded byte budget exceeded");
    }
}
} // namespace

/// @brief Validate every message before building immutable indexed key slots.
std::shared_ptr<const catalog> prepare(definition source)
{
    if (source.languages.empty() || source.languages.size() > 128)
    {
        throw std::invalid_argument("localization language budget exceeded");
    }

    source.fallback = normalize_tag(source.fallback);
    std::set<std::string> tags;
    std::size_t bytes = 0;
    const language* fallback = nullptr;
    for (auto& table : source.languages)
    {
        table.tag = normalize_tag(table.tag);
        if (!tags.insert(table.tag).second || table.messages.size() > 65536 ||
            table.direction > text_direction::right_to_left)
        {
            throw std::invalid_argument("duplicate language or invalid table");
        }

        std::set<std::string> keys;
        for (const auto& entry : table.messages)
        {
            validate_message(entry, bytes);
            if (!keys.insert(entry.key).second)
            {
                throw std::invalid_argument("duplicate localization key");
            }
        }

        if (table.tag == source.fallback)
        {
            fallback = &table;
        }
    }

    if (!fallback)
    {
        throw std::invalid_argument("fallback language is missing");
    }

    auto result = std::make_shared<catalog>();
    for (const auto& entry : fallback->messages)
    {
        result->keys.push_back(entry.key);
    }

    std::sort(result->keys.begin(), result->keys.end());
    result->data = std::move(source);
    for (const auto& table : result->data.languages)
    {
        std::vector<const message*> indexed(result->keys.size());
        for (const auto& entry : table.messages)
        {
            auto found = std::lower_bound(result->keys.begin(), result->keys.end(), entry.key);
            if (found == result->keys.end() || *found != entry.key)
            {
                throw std::invalid_argument("translation key absent from fallback schema");
            }

            indexed[static_cast<std::size_t>(found - result->keys.begin())] = &entry;
        }

        result->entries.push_back(std::move(indexed));
    }

    return result;
}
} // namespace atlas::localization::detail
