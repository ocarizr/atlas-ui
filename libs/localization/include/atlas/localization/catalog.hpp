/**
 * @file
 * @brief Portable validated string tables and cardinal plural categories.
 */
#pragma once
#include <array>
#include <atlas/diagnostics/instrumentation.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace atlas::localization
{
/// @brief Unicode cardinal categories; these are not interchangeable across languages.
enum class plural_category : std::uint8_t
{
    zero, ///< Zero-specific form.
    one,  ///< Singular-category form.
    two,  ///< Dual-category form.
    few,  ///< Small-count category.
    many, ///< Large-count category.
    other ///< Required general fallback form.
};
/// @brief Base paragraph direction; mixed runs are shaped by the text backend.
enum class text_direction : std::uint8_t
{
    left_to_right, ///< Left-origin paragraph.
    right_to_left  ///< Right-origin paragraph.
};

/// @brief One authored key is either a literal or a complete plural message with an other form.
struct message
{
    std::string key;                 ///< Stable case-sensitive content identity.
    std::optional<std::string> text; ///< Literal, including an intentionally empty string.
    std::array<std::optional<std::string>, 6> forms{}; ///< Indexed by plural_category.
};

/// @brief Missing keys in a language fall back through parent tags and the explicit default.
struct language
{
    std::string tag; ///< BCP-47 language tag, canonicalized to ASCII lowercase.
    text_direction direction = text_direction::left_to_right; ///< Base paragraph direction.
    std::vector<message> messages;                            ///< Unique keys within this language.
};

/// @brief Construction data accepted equally from XML or generated native factories.
struct definition
{
    std::string fallback;            ///< Required language containing the complete key schema.
    std::vector<language> languages; ///< Unique language tags; at most 128.
};

/// @brief Validate and canonicalize a bounded BCP-47-shaped tag; does not guess a locale.
[[nodiscard]] std::string normalize_tag(std::string_view tag);
/// @brief Parse at most 8 MiB of XML bytes; never open a file or infer project paths.
[[nodiscard]] definition parse_xml(std::string_view bytes, const diagnostics::services& sinks = {});
/// @brief Platform adapter for cardinal selection; throws when no backend is available.
[[nodiscard]] plural_category select_plural(std::string_view locale, double number);
/// @brief Query whether this build includes native cardinal selection.
[[nodiscard]] bool plural_backend_available() noexcept;
/// @brief Injectable pure selector for another platform or deterministic tests.
using plural_selector = std::function<plural_category(std::string_view, double)>;
} // namespace atlas::localization
