/** @file @brief Lossless native palette records under a strict integrity envelope. */
#include <atlas/mvvm_content/detail/prepared_binary.hpp>
#include <atlas/mvvm_content/detail/prepared_envelope.hpp>
#include <atlas/ui_content/palette.hpp>

namespace atlas::ui_content
{
namespace
{
constexpr mvvm_content::detail::signature magic{std::byte{'A'}, std::byte{'P'}, std::byte{'A'},
                                                std::byte{'L'}}; ///< Palette envelope signature.

/// @brief Native and binary records use identical validation after bounded decoding.
template <class Archive> void fields(Archive& archive, ui::palette_definition& source)
{
    archive.field(source.contract);
    archive.sequence(source.colors, ui::max_palette_colors,
                     [](auto& stream, ui::palette_color& entry)
                     {
                         stream.field(entry.key);
                         for (auto& channel : entry.value)
                         {
                             stream.field(channel);
                         }
                     });
}
} // namespace

/// @brief Preserve exact compiled floating-point channels rather than round-tripping hex text.
std::vector<std::byte> encode_palette(const ui::palette& source)
{
    auto data = source.definition();
    mvvm_content::detail::binary<false> archive;
    fields(archive, data);
    return mvvm_content::detail::seal_prepared(archive, magic, 1);
}

/// @brief Verify extent, version, checksum and all identifiers/channels before publication.
std::shared_ptr<const ui::palette> decode_palette(std::span<const std::byte> bytes)
{
    if (bytes.size() > max_palette_bytes)
    {
        throw std::invalid_argument("Prepared palette too large");
    }

    mvvm_content::detail::binary<true> archive{
        mvvm_content::detail::open_prepared(bytes, magic, 1)};
    ui::palette_definition result;
    fields(archive, result);
    if (!archive.exhausted())
    {
        throw std::invalid_argument("Trailing prepared palette payload");
    }

    return ui::palette::compile(std::move(result));
}
} // namespace atlas::ui_content
