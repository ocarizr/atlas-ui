/** @file @brief Exact bounded draft attributes and canonical scalar encoding. */
#pragma once
#include "document_state.hpp"
#include <array>
#include <charconv>
#include <pugixml.hpp>
#include <set>

namespace atlas::editor_flow::detail
{
/// @brief Every attribute is consumed exactly once, with no implicit numeric conversion.
class fields
{
  public:
    fields(pugi::xml_node node, bool reading) : node_{node}, reading_{reading}
    {
    } ///< Borrow one record.

    /// @brief Read or emit an exact scalar attribute; mandatory fields must exist even if empty.
    template <class T> void value(const char* name, T& data, bool required = true)
    {
        known_.insert(name);
        if (reading_)
        {
            const auto attr = node_.attribute(name);
            if (!attr)
            {
                if (required)
                {
                    throw std::invalid_argument(std::string{"Missing Flow draft attribute: "} +
                                                name);
                }

                return;
            }

            const std::string_view text = attr.value();
            if constexpr (std::is_same_v<T, std::string>)
            {
                data = text;
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                if (text != "true" && text != "false")
                {
                    throw std::invalid_argument("Invalid Flow draft Boolean");
                }

                data = text == "true";
            }
            else
            {
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), data);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                {
                    throw std::invalid_argument("Invalid Flow draft number");
                }
            }
        }
        else
        {
            if constexpr (std::is_same_v<T, std::string>)
            {
                node_.append_attribute(name).set_value(data.c_str());
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                node_.append_attribute(name).set_value(data ? "true" : "false");
            }
            else
            {
                std::array<char, 64> text{};
                const auto encoded = std::to_chars(text.data(), text.data() + text.size(), data);
                if (encoded.ec != std::errc{})
                {
                    throw std::invalid_argument("Cannot encode Flow draft scalar");
                }

                node_.append_attribute(name).set_value(
                    text.data(), static_cast<std::size_t>(encoded.ptr - text.data()));
            }
        }
    }

    void optional_bool(const char* name,
                       std::optional<bool>& data); ///< Preserve absent versus false.
    void finish() const; ///< Reject unknown or duplicate record attributes.

    [[nodiscard]] pugi::xml_node node() const
    {
        return node_;
    } ///< Borrow the current XML record.

    [[nodiscard]] bool reading() const
    {
        return reading_;
    } ///< Select the structural codec direction.
  private:
    pugi::xml_node node_;              ///< DOM-owned node.
    bool reading_;                     ///< Parsing versus canonical generation.
    std::set<std::string_view> known_; ///< Accepted attribute spellings.
};

/// @brief Comments are allowed; text, entities and processing instructions are never draft content.
void empty(pugi::xml_node node);

/// @brief Bound emitted XML before retaining its owned snapshot.
struct writer final : pugi::xml_writer
{
    std::string bytes;                                       ///< Bounded canonical XML.
    void write(const void* data, std::size_t size) override; ///< Append a checked serializer chunk.
};

/// @brief Reuse the exact four MVVM scalar types for parameters and equality guards.
void scalar_fields(fields& codec, mvvm::value& value);
/// @brief Encode editor item kind and persistent ID.
void reference_fields(fields& codec, item_ref& item);
void region_fields(fields& codec, ui_content::flow_region& value);         ///< Draft region codec.
void screen_fields(fields& codec, ui_content::flow_screen& value);         ///< Draft screen codec.
void transition_fields(fields& codec, ui_content::flow_transition& value); ///< Draft route codec.
void parameter_fields(fields& codec, ui_content::flow_parameter& value);   ///< Exact scalar codec.
void layout_fields(fields& codec, item_layout& value); ///< Editor-only canvas metadata.
} // namespace atlas::editor_flow::detail
