/** @file @brief Shared exact XML attribute codec for UI authoring data. */
#pragma once
#include <algorithm>
#include <array>
#include <atlas/ui_content/document.hpp>
#include <charconv>
#include <cmath>
#include <pugixml.hpp>
#include <type_traits>

namespace atlas::ui_content::detail
{
/// @brief Read or write a named set of fields and reject undeclared attributes.
class fields
{
  public:
    fields(pugi::xml_node node, bool reading) : node_{node}, reading_{reading}
    {
    } ///< Borrow node.

    /// @brief An absent optional attribute leaves the authored default in place.
    template <class T> void value(const char* name, T& data, bool required = false)
    {
        if (reading_)
        {
            if (const auto attribute = take(name, required))
            {
                data = decode<T>(attribute.value());
            }

            return;
        }

        node_.append_attribute(name).set_value(encode(data).c_str());
    }

    /// @brief Persist a closed enum by stable name in its declared vocabulary.
    template <class Enum, std::size_t N>
    void choice(const char* name, Enum& data, const std::array<const char*, N>& names)
    {
        if (reading_)
        {
            if (const auto attribute = take(name, false))
            {
                const auto found = std::ranges::find(names, std::string_view{attribute.value()});
                if (found == names.end())
                {
                    throw std::invalid_argument(std::string{"Unknown value for "} + name);
                }

                data = static_cast<Enum>(found - names.begin());
            }

            return;
        }

        node_.append_attribute(name).set_value(names.at(static_cast<std::size_t>(data)));
    }

    /// @brief Decode a typed persistent ID; the containing attribute fixes its type.
    void reference(const char* name, assets::asset_ref& data, assets::asset_type type);
    /// @brief Decode an optional reference without creating an invalid default ID.
    void reference(const char* name, std::optional<assets::asset_ref>& data,
                   assets::asset_type type);

    /// @brief Leave default or unset values absent, while recording the field on reads.
    template <class T> void optional(const char* name, T& data, bool present)
    {
        if (reading_ || present)
        {
            value(name, data);
        }
    }

    /// @brief Track a known field even when absent.
    [[nodiscard]] pugi::xml_attribute take(const char* name, bool required);
    void finish() const; ///< Reject unknown and duplicate attributes.

    [[nodiscard]] bool reading() const noexcept
    {
        return reading_;
    } ///< Codec direction.

    [[nodiscard]] pugi::xml_node node() const noexcept
    {
        return node_;
    } ///< Current XML element.

  private:
    /// @brief Decode without coercion, partial consumption, nonfinite values or lossy narrowing.
    template <class T> static T decode(std::string_view text)
    {
        if constexpr (std::is_same_v<T, std::string>)
        {
            mvvm::validate_text(text, 65536);
            return std::string{text};
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            if (text != "true" && text != "false")
            {
                throw std::invalid_argument("Invalid UI XML boolean: " + std::string{text});
            }

            return text == "true";
        }
        else
        {
            T result{};
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            {
                throw std::invalid_argument("Invalid UI XML number: " + std::string{text});
            }

            if constexpr (std::is_floating_point_v<T>)
            {
                if (!std::isfinite(result))
                {
                    throw std::invalid_argument("Nonfinite UI XML number");
                }
            }

            return result;
        }
    }

    /// @brief Emit round-trip numeric precision using locale-independent formatting.
    template <class T> static std::string encode(const T& data)
    {
        if constexpr (std::is_same_v<T, std::string>)
        {
            mvvm::validate_text(data, 65536);
            return data;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return data ? "true" : "false";
        }
        else
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                if (!std::isfinite(data))
                {
                    throw std::invalid_argument("Nonfinite UI XML number");
                }
            }

            std::array<char, 64> buffer{};
            const auto formatted =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), data);
            if (formatted.ec != std::errc{})
            {
                throw std::invalid_argument("Cannot encode UI XML scalar");
            }

            return {buffer.data(), formatted.ptr};
        }
    }

    pugi::xml_node node_;                 ///< Borrowed XML node.
    bool reading_;                        ///< Reader or writer.
    std::vector<std::string_view> known_; ///< Names declared by the codec for this node.
};

void expect_empty(pugi::xml_node node); ///< Reject content in attribute-only XML nodes.
void validate(const document& source);  ///< Validate document-local identities and references.
void read_element(pugi::xml_node node, element& target); ///< Decode one element and its sections.
void write_element(pugi::xml_node node, const element& source);  ///< Encode one element.
void read_binding(pugi::xml_node node, element& target);         ///< Decode a descriptor child.
void write_bindings(pugi::xml_node node, const element& source); ///< Encode ordered descriptors.
} // namespace atlas::ui_content::detail
